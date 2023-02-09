# Assingment-7-Buildroot
## Analysis for faulty-oops.


```
Unable to handle kernel NULL pointer dereference at virtual address 0000000000000000

pc : faulty_write+0x14/0x20 [faulty]

Call trace:
 faulty_write+0x14/0x20 [faulty]


Disassembly of section .text:

0000000000000000 <faulty_write>:
   0:	d503245f 	bti	c
   4:	d2800001 	mov	x1, #0x0                   	// #0
   8:	d2800000 	mov	x0, #0x0                   	// #0
   c:	d503233f 	paciasp
  10:	d50323bf 	autiasp
  14:	b900003f 	str	wzr, [x1]
  18:	d65f03c0 	ret
  1c:	d503201f 	nop

```

### Line 1
`Unable to handle kernel NULL pointer dereference at virtual address 0000000000000000`
Looking at this line we can get a hint that the oops was caused by accessing a Null Pointer

### Lines 2 & 3
```
pc : faulty_write+0x14/0x20 [faulty]

Call trace:
 faulty_write+0x14/0x20 [faulty]
```

Looking at the ProgramCounter or the Call trace at the time of the oops shows that the function `faulty_write` was the one causing the section.

### Looking deaper in the faulty_write function.
The Call trace showed that the command that caused the oops was inside the function `faulty_write` exactly at the offset 0x14.
By looking in the Disassembly of the faulty module using the objdump utility we can see that at offset 0x14 the command 
`str wzr, [x1]` exist which tries to write to the location addressed by the Null pointer causing the oops.

