; Reversing a String (RevStr.asm)
; This program reverses a string.

INCLUDE Irvine32.inc

.data
nameSize = 80
aName byte namesize+1 DUP (?)
prompt byte "Enter String: ",0
promptsize = $ - prompt

.code
main proc

  mov edx, OFFSET prompt
  mov ecx, promptsize
  call WriteString

	mov	edx, OFFSET aName
	mov	ecx, nameSize

  call ReadString

	mov	 esi,0
  mov ecx, nameSize
  push 0

  ; Push the name on the stack.
	L1:	
    movzx ebx,aName[esi]	; get character
    cmp ebx,0
    je L1_Escape
		push ebx							; push on stack
		inc	 esi
		loop L1

  L1_Escape:
    mov	 ecx, nameSize
    mov	 esi,0
	; Pop the name from the stack in reverse
	; and store it in the aName array.

	L2:
    pop  ebx							; get character
    cmp ebx, 0
    je L2_Escape
    mov	 aName[esi],bl		; store in string
    inc	 esi
    loop L2

  L2_Escape:
    mov	edx, OFFSET aName
    call WriteString
    call Crlf

	exit
main endp
end main
