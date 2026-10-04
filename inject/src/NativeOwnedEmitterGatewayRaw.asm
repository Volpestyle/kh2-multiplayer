OPTION CASEMAP:NONE
.code
Kh2OwnedEmitterGatewayHandler PROTO
PUBLIC Kh2OwnedEmitterGatewayRaw
PUBLIC Kh2OwnedEmitterGatewayPublished
PUBLIC Kh2OwnedEmitterGatewayReturned
PUBLIC Kh2OwnedEmitterGatewayRetired
PUBLIC Kh2OwnedEmitterGatewayEnd
PUBLIC Kh2OwnedEmitterGatewayHandler
; RCX stable PrimitiveState*, RDX validated original dispatcher, R8 controller,
; R9 immutable invocation. Frame slots at actual pre-CALL T retain ownership;
; no inherited TLS or helper between publishing T and CALL.
Kh2OwnedEmitterGatewayRaw PROC FRAME:Kh2OwnedEmitterGatewayHandler
    push r12
    .pushreg r12
    push r13
    .pushreg r13
    push r14
    .pushreg r14
    push r15
    .pushreg r15
    sub rsp,48h
    .allocstack 48h
    .endprolog
    mov r12,rcx
    mov r15,rdx
    mov r14,r8
    mov r13,r9
    mov [rsp+20h],r12
    mov [rsp+28h],r13
    lea rax,[rsp-180h]
    mov [r12+8],rax
    lea rax,[rsp-188h]
    mov [r12+16],rax
    lea rax,[rsp-8]
    mov [r12+24],rax
    lea rax,[rsp-58h]
    mov [r12+32],rax
    lea rax,Kh2OwnedEmitterGatewayReturned
    mov [r12+40],rax
    mov [r12+200],r13
Kh2OwnedEmitterGatewayPublished LABEL BYTE
    mov qword ptr [r12],1
    mov rcx,r14
    xor edx,edx
    call r15
Kh2OwnedEmitterGatewayReturned LABEL BYTE
    mov qword ptr [r12],0
Kh2OwnedEmitterGatewayRetired LABEL BYTE
    mov qword ptr [r12+152],1
    add rsp,48h
    pop r15
    pop r14
    pop r13
    pop r12
    ret
Kh2OwnedEmitterGatewayEnd LABEL BYTE
Kh2OwnedEmitterGatewayRaw ENDP

; Actual x64 language handler. Search phase observes nothing; unwind retires
; only this assembly frame's published activation. DispatcherContext.ControlPc
; bounds avoid using not-yet-initialized stack locals during partial prologue.
; Return ExceptionContinueSearch (1), never swallow/redirect the exception.
Kh2OwnedEmitterGatewayHandler PROC
    test dword ptr [rcx+4],6
    jz search
    mov r10,[r9]
    lea r11,Kh2OwnedEmitterGatewayPublished
    cmp r10,r11
    jb search
    lea r11,Kh2OwnedEmitterGatewayRetired
    cmp r10,r11
    jae search
    mov r10,[rdx+20h]
    mov qword ptr [r10],0
    mov qword ptr [r10+160],1
    mov qword ptr [r10+144],0
    mov qword ptr [r10+208],5
    mov qword ptr [r10+248],1
search:
    mov eax,1
    ret
Kh2OwnedEmitterGatewayHandler ENDP
END
