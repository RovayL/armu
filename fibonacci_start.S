.text
.global _start
.extern main

_start:
    bl main
    brk #0
