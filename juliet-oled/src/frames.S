.section .rodata.embedded,"a",@progbits
.global juliet_frames
.global juliet_frames_end
.align 4
juliet_frames:
.incbin "frames.bin"
juliet_frames_end:
.size juliet_frames, juliet_frames_end - juliet_frames
