org 0x7c00

; 
; ʵģʽ�ڴ沼��
;
; ��ʼ��ַ    ��С       ��;
; ----------------------------------------
; 0x000      1KB       �ж�������
; 0x400      256B      BIOS ������
; 0x500      29.75KB   ��������
; 0x7C00     512B      MBR ��������
; 0x7E00     607.6KB   ��������
; 0x9FC00    1KB       ��չ BIOS ������
; 0xA0000    64KB      ���ڲ�ɫ��ʾ������
; 0xB0000    32KB      ���ںڰ���ʾ������
; 0xB8000    32KB      �����ı���ʾ������
; 0XC0000    32KB      ��ʾ������ BIOS
; 0XC8000    160KB     ӳ���ڴ�
; 0xF0000    64KB-16B  ϵͳBIOS
; 0xFFFF0    16B       ϵͳ BIOS ��ڵ�ַ
; 

; vesa support
;
;   https://github.com/FlowerBlackG/YurongOS/blob/master/src/boot/boot.asm
;   added by GTY
vesa_video_mode equ 0x143
vesa_video_mode_code equ (vesa_video_mode | 0x4000)

;section .code16
[BITS 16]
start:

%ifdef USE_VESA
		; ��ȡ VESA ��Ϣ��
		xor ax, ax
		mov es, ax
		mov di, 0x7e00
		mov ax, 0x4f01
		mov cx, vesa_video_mode
		int 0x10

		; ������ĻģʽΪ�ı�ģʽ���������Ļ��
		; �ж�ָ���Ϊ 10H���� AH=0H ʱ��ʾ������ʾģʽ��ģʽ����Ϊ AL��
		; AL=3H ��ʾ�ı�ģʽ��80��25��16ɫ��
		; AL=12H ��ʾͼ��ģʽ��VGA 640��480 16ɫ
		; AX=0x4F02, BX=0x4180 ��ʾ 1440��900 32λɫ
		; AX=0x4F02, BX=0x4143 ��ʾ 800��600 32λɫ

		mov bx, vesa_video_mode_code
		mov ax, 0x4F02
		int 0x10
%endif


lgdt [gdtr]
		
cli

;��a20 ��ַ��
in al,92h
or al,00000010b
out 92h, al

; ���뱣��ģʽ
mov eax, cr0;
or eax, 1;
mov cr0, eax
			
jmp dword 0x8:_startup ;����cs�μĴ���

	
;section .code32
[BITS 32]
_startup:

		; ���������μĴ���
		mov ax, 0x10
		mov ds, ax
		mov es, ax
		mov ss, ax

		; �Ӵ��̼����ں�
		mov	ecx, KERNEL_SIZE 	;cx = ������KERNEL_SIZE����Ϊloop�Ĵ���
		mov eax, 1				;LBAѰַģʽ��sector��Ŵ�0��ʼ��  #0������������#1������ʼ����kernel��������
		mov ebx, 0x100000		;Ŀ���ŵ�ַ��1M����ʼ��ÿ��loop����512 bytes

_load_kernel:
		push eax
		inc eax
		
		push ebx
		add	ebx, 512
		call _load_sector
		loop _load_kernel		
		
		;�޸����мĴ�������λ��ַ
		mov ax, 0x20
		mov ds, ax
		mov es, ax
		mov ss, ax
		or esp, 0xc0000000
		jmp 0x18:0xc0100000
		
_load_sector:
	push ebp
	mov ebp,esp
	
	push edx
	push ecx
	push edi
	push eax		
	
	mov al,1		;��1������
	mov dx,1f2h		;�������Ĵ��� 0x1f2
	out dx,al
	
	mov eax,[ebp+12] ;[ebp+12]��Ӧ����mov eax, 1   push eaxָ����ջ��ֵ��eaxΪҪ�����������
					;LBA28(Linear Block Addressing)ģʽ���������ŵ�Bits 7~0�� ��28 Bits������
	inc dx			;�����żĴ��� 0x1f3
	out dx,al
	
	shr eax,8		;LBA28(Linear Block Addressing)ģʽ���������ŵ�Bits 15~8 ����AL�У� ��28 Bits������
	inc dx			;Port��DX = 0x1f3+1 = 0x1f4  
	out dx,al
	
	shr eax,8		;LBA28(Linear Block Addressing)ģʽ���������ŵ�Bits 23~16����AL�У� ��28 Bits������
	inc dx			;Port��DX = 0x1f4+1 = 0x1f5 
	out dx,al
	
	shr eax,8
	and al,0x0f
	or al,11100000b ;Bit(7��5)Ϊ1��ʾ��IDE�ӿڣ�Bit(6)Ϊ1��ʾ����LBA28ģʽ��Bit(4)Ϊ1��ʾ���̡�
					;Bit(3~0)ΪLBA28�е�Bit27~24λ
	inc dx			;Port��DX = 0x1f5+1 = 0x1f6 
	out dx,al
	
	mov al,0x20		;0x20��ʾ��1��sector��0x30��ʾд1��sector
	inc dx			;Port��DX = 0x1f6+1 = 0x1f7 
	out dx,al
	
.test:
	in al,dx
	test al,10000000b
	jnz .test
	
	test al,00001000b
	jz .load_error
	
	
	mov ecx,512/4
	mov dx,0x1f0
	mov edi,[ebp+8]	;ȡ��callǰ��ջ����[ebp+8] = 0x100000  = 1MB
	rep insd
	xor ax,ax
	jmp .load_exit
	
.load_error:
	mov dx,0x1f1
	in al,dx
	xor ah,ah
			
.load_exit:
	
	pop eax		
	pop edi
	pop ecx
	pop edx
	leave		;Destory stack frame
	retn 8		
		
;section .data
KERNEL_SIZE		equ		(398)	    


gdt:					;  gdt��ͷ��ռλ������
dw 0x0000
dw 0x0000
dw 0x0000
dw 0x0000		;GDT���0�����Ϊȫ��

; boot����ʱʹ�õĴ���Σ���ѡ����0x8
dw 0xFFFF 		; limit(L15~0) =0xffff
dw 0x0000 		; base(B15~0)=0x0000
dw 0x9A00 		; P=1 , �δ����ڴ棻DPL=0 , ��Ȩ����TYPE=1010 , ִ��/�ɶ���B23~16 = 0x00
dw 0x00CF		; B31~24 = 0x00��; G=1 , ���޵�λΪ 4KB��D/B=1 , 32bit��L19~16=0xf

; boot����ʱʹ�õ����ݶΣ���ѡ����0x10
dw 0xFFFF 		; limit(L15~0)=0xffff
dw 0x0000 		; base(B15~0)=0x0000
dw 0x9200 		; P=1 , �δ����ڴ棻DPL=0 , ��Ȩ����TYPE=0010 , �ɶ�/д��B23~16 = 0x00
dw 0x00CF 		; B31~24 = 0x00��G=1 , ���޵�λΪ 4KB��D/B=1 , 32bit��L19~16=0xf

; �ں˳�ʼ���׶εĴ���Σ���ѡ����0x18
dw 0xFFFF 		; limit(L15~0)=0xffff��
dw 0x0000 		; base(B15~0)=0x8000
dw 0x9A00 		; P=1 , �δ����ڴ棻DPL=3 , ��Ȩ����TYPE=0010 , �ɶ�/д��B23~16 = 0x0B
dw 0x40CF 		; B31~24 = 0x00��G=1 , ���޵�λΪ 4KB��D/B=1 , 32bit��L19~16=0xf

; �ں˳�ʼ���׶ε����ݶΣ���ѡ����0x20
dw 0xFFFF 		; limit(L15~0)=0xffff
dw 0x0000 		; base(B15~0)=0x0000
dw 0x9200 		; P=1 , �δ����ڴ棻DPL=3 , ��Ȩ����TYPE=0010 , �ɶ�/д��B23~16 = 0x00
dw 0x40CF 		; B31~24 = 0x00��G=1 , ���޵�λΪ 4KB��D/B=1 , 32bit��L19~16=0xf

; GDT End

gdtr:
		dw $-gdt		;limit
		dd gdt			;offset

		dw 0xabfb  ; just a marker

		times 510 - ($ - $$) db 0
		
		dw 0xAA55
