.model flat, stdcall
.stack 4096

.data
    msg db "Windows Assembly Output Test", 13, 10, 0
    msg_len = $ - msg
    bytesWritten dw 0

.code
start:
    ; Get stdout handle
    push -11 ; STD_OUTPUT_HANDLE
    push 0
    push 0
    call GetStdHandle

    ; WriteFile(hStdOut, msg, msg_len, &bytesWritten, NULL)
    push 0              ; lpOverlapped
    push offset bytesWritten
    push msg_len
    push offset msg
    push eax           ; hStdOut (returned from GetStdHandle)
    push 0             ; dwFlags
    call WriteFile

    ; ExitProcess(0)
    push 0
    call ExitProcess
end start
