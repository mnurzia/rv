#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../rv.h"
#include "../rv_clint.h"
#include "../rv_plic.h"
#include "../rv_uart.h"

unsigned long ninstr = 0;
rv_u32 gres = 0;

#define MACHINE_RAM_BASE 0x80000000UL
#define MACHINE_RAM_SIZE (1024UL * 1024UL * 128UL) /* 128MiB */

#define MACHINE_PLIC_BASE 0xC000000UL
#define MACHINE_PLIC_SIZE 0x400000UL

#define MACHINE_CLINT_BASE 0x2000000UL
#define MACHINE_CLINT_SIZE 0x10000UL

#define MACHINE_UART_BASE 0x3000000UL
#define MACHINE_UART_SIZE 0x100UL * 4

#define MACHINE_UART2_BASE 0x6000000UL
#define MACHINE_UART2_SIZE 0x100UL * 4

typedef struct machine {
  rv *cpu;
  rv_u32 *ram;
  rv_u8 recv_buf;
  int has_recv;
  int gdb_socket;
  rv_plic plic;
  rv_clint clint;
  rv_uart uart, uart2;
} machine;

void stl(rv *cpu) {
  printf("%lu pc %08X a0 %08X a1 %08X a2 %08X a3 %08X a4 %08X a7 %08X s0 %08X "
         "s1 %08X "
         "s2 %08X "
         "ra %08X "
         "sp %08X r %08X p%i\n",
         ninstr, cpu->pc, cpu->r[10], cpu->r[11], cpu->r[12], cpu->r[13],
         cpu->r[14], cpu->r[17], cpu->r[8], cpu->r[9], cpu->r[18], cpu->r[1],
         cpu->r[2], gres, cpu->priv);
}

rv_res mach_bus(void *user, rv_u32 addr, rv_u32 *data, rv_u32 store) {
  machine *mach = (machine *)user;
  if (addr >= MACHINE_RAM_BASE && addr < MACHINE_RAM_BASE + MACHINE_RAM_SIZE) {
    rv_u32 *ram = mach->ram + ((addr - MACHINE_RAM_BASE) >> 2);
    if (store)
      *ram = *data;
    else
      *data = *ram;
    return RV_OK;
  } else if (addr >= MACHINE_PLIC_BASE &&
             addr < MACHINE_PLIC_BASE + MACHINE_PLIC_SIZE) {
    return rv_plic_bus(&mach->plic, addr - MACHINE_PLIC_BASE, data, store);
  } else if (addr >= MACHINE_CLINT_BASE &&
             addr < MACHINE_CLINT_BASE + MACHINE_CLINT_SIZE) {
    return rv_clint_bus(&mach->clint, addr - MACHINE_CLINT_BASE, data, store);
  } else if (addr >= MACHINE_UART_BASE &&
             addr < MACHINE_UART_BASE + MACHINE_UART_SIZE) {
    return rv_uart_bus(&mach->uart, addr - MACHINE_UART_BASE, data, store);
  } else if (addr >= MACHINE_UART2_BASE &&
             addr < MACHINE_UART2_BASE + MACHINE_UART2_SIZE) {
    return rv_uart_bus(&mach->uart2, addr - MACHINE_UART2_BASE, data, store);
  } else {
    return RV_BAD;
  }
}

#define rv_bf(i, h, l)                                                         \
  (((i) >> (l)) & ((1 << ((h) - (l) + 1)) - 1))    /* extract bit field */
#define rv_b(i, l) rv_bf(i, l, l)                  /* extract bit */
#define rv_tb(i, l, o) (rv_b(i, l) << (o))         /* translate bit */
#define rv_tbf(i, h, l, o) (rv_bf(i, h, l) << (o)) /* translate bit field */

void dump_pt(rv *cpu, rv_u32 base) {
  rv_u32 satp = cpu->csrs.satp;
  rv_u32 pt_addr = base ? base : rv_tbf(satp, 21, 0, 12), pt2_addr;
  rv_u32 i, j;
  printf("Page table dump @ %08X:\n", pt_addr);
  for (i = 0; i < (1 << 10); i++) {
    rv_u32 pte, pte_addr = pt_addr + (i << 2);
    rv_res res = cpu->bus_cb(cpu->user, pte_addr, &pte, 0);
    rv_u32 phys_lo, phys_hi;
    if (res) {
      printf("%08X: fault\n", pte_addr);
      continue;
    }
    if (!pte)
      continue;
    phys_lo = rv_bf(pte, 31, 20) << 22;
    phys_hi = phys_lo + ((1 << 22) - 1);
    printf("%08X: %08X [%08X-%08X]\n", (i << 22), pte, phys_lo, phys_hi);
    if (i << 22 != 0x95400000)
      continue;
    pt2_addr = rv_tbf(pte, 31, 10, 12);
    for (j = 0; j < (1 << 10); j++) {
      rv_u32 pte2_addr = pt2_addr + (j << 2), pte2;
      rv_u32 phys_lo_2, phys_hi_2;
      res = cpu->bus_cb(cpu->user, pte2_addr, &pte2, 0);
      if (res) {
        printf("  %08X: fault\n", pte2_addr);
        continue;
      }
      phys_lo_2 = rv_bf(pte2, 19, 10) << 12;
      phys_hi_2 = phys_lo_2 + ((1 << 12) - 1);
      if (!pte2)
        continue;
      printf("  (%03X) %08X %08X: %08X [%08X-%08X]\n", j, pte2_addr,
             (i << 22) + (j << 12), pte2, phys_lo + phys_lo_2,
             phys_hi + phys_hi_2);
    }
  }
}

int open_sock(void) {
  int sock, new;
  struct sockaddr_in addr;
  int addrlen = sizeof(addr);
  if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    printf("\n Socket creation error \n");
    return 1;
  }

  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = inet_addr("172.24.0.2");
  addr.sin_port = htons(61444);

  if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("bind failed");
    shutdown(sock, SHUT_RDWR);
    exit(EXIT_FAILURE);
  }
  if (listen(sock, 3) < 0) {
    perror("listen");
    shutdown(sock, SHUT_RDWR);
    exit(EXIT_FAILURE);
  }
  if ((new = accept(sock, (struct sockaddr *)&addr, (socklen_t *)&addrlen)) <
      0) {
    perror("accept");
    shutdown(sock, SHUT_RDWR);
    exit(EXIT_FAILURE);
  }
  return new;
}

rv_res uart_io(void *user, rv_u8 *byte, rv_u32 w) {
  (void)(user);
  static int throttle = 0;
  if (w) {
    write(STDOUT_FILENO, byte, 1);
  } else {
    int rv;
    struct pollfd fd;
    rv_u8 b;
    ssize_t rvs;
    memset(&fd, 0, sizeof(fd));
    fd.events = POLLIN;
    fd.fd = STDIN_FILENO;
    if (throttle++ & 0xFF)
      return RV_BAD;
    rv = poll(&fd, 1, 0);
    if (!rv || rv < 0 || !(fd.revents & POLLIN)) {
      return RV_BAD;
    }
    rvs = read(STDIN_FILENO, &b, 1);
    if (rvs != 1)
      return RV_BAD;
    *byte = b;
  }
  return RV_OK;
}

rv_res uart2_io(void *user, rv_u8 *byte, rv_u32 w) {
  machine *m = user;
  static int throttle = 0;
  if (w) {
    write(m->gdb_socket, byte, 1);
  } else {
    int rv;
    struct pollfd fd;
    rv_u8 b;
    ssize_t rvs;
    memset(&fd, 0, sizeof(fd));
    fd.events = POLLIN;
    fd.fd = m->gdb_socket;
    if (throttle++ & 0xFF)
      return RV_BAD;
    rv = poll(&fd, 1, 0);
    if (!rv || rv < 0 || !(fd.revents & POLLIN))
      return RV_BAD;
    rvs = read(m->gdb_socket, &b, 1);
    if (rvs != 1)
      return RV_BAD;
    *byte = b;
  }
  return RV_OK;
}

int main(int argc, const char *const *argv) {
  rv cpu;
  machine mach;
  rv_u32 dtb_addr;
  rv_u32 period = 0;
  unsigned long instr_limit = 0;
  if (argc >= 4)
    sscanf(argv[3], "%lu", &instr_limit);

  mach.recv_buf = 0;
  mach.has_recv = 0;
  (void)argc;
  (void)argv;
  mach.ram = malloc(MACHINE_RAM_SIZE);
  mach.cpu = &cpu;
  mach.gdb_socket = open_sock();
  memset(mach.ram, 0, MACHINE_RAM_SIZE);
  {
    long sz;
    FILE *f = fopen(argv[1], "rb");
    fseek(f, 0L, SEEK_END);
    sz = ftell(f);
    assert(sz > 0);
    fseek(f, 0L, SEEK_SET);
    fread(mach.ram, 1, (unsigned long)sz, f);
    fclose(f);
    printf("Loaded %li byte kernel image.\n", (unsigned long)sz);
  }
  {
    long sz;
    FILE *f = fopen(argv[2], "rb");
    fseek(f, 0L, SEEK_END);
    sz = ftell(f);
    assert(sz > 0);
    fseek(f, 0L, SEEK_SET);
    dtb_addr = MACHINE_RAM_BASE + 0x2200000; /* DTB should be aligned */
    fread(mach.ram + ((dtb_addr - MACHINE_RAM_BASE) >> 2), 1, (unsigned long)sz,
          f);
    fclose(f);
    printf("Loaded %li byte DTB.\n", (unsigned long)sz);
  }
  rv_init(&cpu, &mach, &mach_bus);
  rv_plic_init(&mach.plic);
  rv_clint_init(&mach.clint, &cpu);
  rv_uart_init(&mach.uart, NULL, &uart_io);
  rv_uart_init(&mach.uart2, &mach, &uart2_io);
  cpu.pc = MACHINE_RAM_BASE;
  /* https://lwn.net/Articles/935122/ */
  cpu.r[10] /* a0 */ = 0;        /* hartid */
  cpu.r[11] /* a1 */ = dtb_addr; /* dtb ptr */
  {
    do {
      rv_u32 irq = 0, pprv = cpu.priv;
      if (!((period = (period + 1)) & 0xFFF))
        if (!++cpu.csrs.mtime)
          cpu.csrs.mtimeh++;
      /*if (gres == RV_EBP)*/
      /*if (ninstr >= 95756672 - 1000 && ninstr <= 95756672 + 100)*/
      /*if (cpu.pc == 0xc0035f18 || cpu.pc == 0xc0033b8c ||)*/
      /*if (cpu.pc == 0xc017bf26)
        stl(&cpu);*/
      /*if (ninstr == 143123149 || ninstr == 143129096)
        stl(&cpu);*/
      gres = rv_step(&cpu);
      /*if (gres == RV_EUECALL || (cpu.priv != pprv && pprv == RV_PUSER))
        stl(&cpu);*/
      if (rv_uart_update(&mach.uart))
        rv_plic_irq(&mach.plic, 1);
      if (rv_uart_update(&mach.uart2))
        rv_plic_irq(&mach.plic, 2);
      irq = RV_CSW * rv_clint_msi(&mach.clint, 0) |
            RV_CTIM * rv_clint_mti(&mach.clint, 0) |
            RV_CEXT * rv_plic_mei(&mach.plic, 0);
      rv_irq(&cpu, irq);
    } while ((instr_limit == 0 || ++ninstr < instr_limit));
    stl(&cpu);
  }
  return EXIT_FAILURE;
}
