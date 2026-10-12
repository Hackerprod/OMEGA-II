OPTION CASEMAP:NONE
.code
known_retired_loop PROC
    mov     rax, rcx
    xor     rdx, rdx
loop_start:
    add     rdx, 1
    dec     rax
    jnz     loop_start
    mov     rax, rdx
    ret
known_retired_loop ENDP
END
