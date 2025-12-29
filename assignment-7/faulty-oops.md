# Analysis of Kernel Oops - Faulty Module

## 1. The Captured Oops Message

```text
Unable to handle kernel NULL pointer dereference at virtual address 0000000000000000
Mem abort info:
  ESR = 0x0000000096000044
  EC = 0x25: DABT (current EL), IL = 32 bits
...
Internal error: Oops: 0000000096000044 [#1] SMP
Modules linked in: faulty(O) hello(O)
CPU: 0 UID: 0 PID: 97 Comm: sh Tainted: G           O       6.12.27 #1
pc : faulty_write+0x8/0x10 [faulty]
lr : vfs_write+0xb4/0x390
...
Call trace:
 faulty_write+0x8/0x10 [faulty]
 ksys_write+0x74/0x110
...
