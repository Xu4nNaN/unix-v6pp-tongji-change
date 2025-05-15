/* 内核的初始化 */

#include "Video.h"
#include "Simple.h"
#include "IOPort.h"
#include "Chip8253.h"
#include "Chip8259A.h"
#include "Machine.h"
#include "IDT.h"
#include "Assembly.h"
#include "Kernel.h"
#include "TaskStateSegment.h"

#include "PageDirectory.h"
#include "PageTable.h"
#include "SystemCall.h"

#include "Exception.h"
#include "DMA.h"
#include "CRT.h"
#include "TimeInterrupt.h"
#include "PEParser.h"
#include "CMOSTime.h"
#include "./Lib.h"

#include "vesa/svga.h"
#include "vesa/console.h"

#include "libyrosstd/sys/types.h"
#include "libyrosstd/string.h"

bool isInit = false;

extern "C" void MasterIRQ7()
{
	SaveContext();
	
	Diagnose::Write("IRQ7 from Master 8259A!\n");
	
	//需要在中断处理程序末尾先8259A发送EOI命令
	//实验发现：有没有下面IOPort::OutByte(0x27, 0x20);这句运行效果都一样，本来以为
	//发送EOI命令之后会有后续的IRQ7中断进入， 但试下来结果是IRQ7只会产生一次。
	IOPort::OutByte(Chip8259A::MASTER_IO_PORT_1, Chip8259A::EOI);

	RestoreContext();

	Leave();

	InterruptReturn();
}


static void callCtors()
{
	extern void (*__CTOR_LIST__)();
	extern void (* __CTOR_END__)();
	
	
	void (**constructor)() = &__CTOR_LIST__;

	
	//constructor++;   
		/*  (可以先看一下链接脚本：Link.ld)
		Link script中修改过后，这里的total已经不是constructor的个数了，
		_CTOR_LIST__的第一个单元开始就是global/static对象的constructor，
		所以不用 constructor++; 
		*/
	
	while(constructor != &__CTOR_END__) //total不是constructor的数量，而是用于检测是否到了_CTOR_LIST__的末尾
	{
		(*constructor)();
		constructor++;
	}
}

static void initBss() {  // https://github.com/FlowerBlackG/YurongOS/blob/master/src/misc/main.cpp
	extern unsigned int __BSS_START__;
    extern unsigned int __BSS_END__;


    unsigned int bssStart = (unsigned int) &__BSS_START__;
    unsigned int bssEnd = (unsigned int) &__BSS_END__;

    for (unsigned int pos = bssStart; pos < bssEnd; pos++) {
        * ((char*) pos) = 0;
    }
}


static void callDtors()
{
	extern void (* __DTOR_LIST__)();
	extern void (* __DTOR_END__)();
	
	void (**deconstructor)() = &__DTOR_LIST__;
	
	while(deconstructor != &__DTOR_END__)
	{
		(*deconstructor)();
		++deconstructor;
	}
}

void main0(void)
{
	Machine& machine = Machine::Instance();

	Chip8253::Init(60);	//初始化时钟中断芯片
	Chip8259A::Init();
	Chip8259A::IrqEnable(Chip8259A::IRQ_TIMER);		
	DMA::Init();
	Chip8259A::IrqEnable(Chip8259A::IRQ_IDE);
	Chip8259A::IrqEnable(Chip8259A::IRQ_SLAVE);
	Chip8259A::IrqEnable(Chip8259A::IRQ_KBD);

	//init gdt
	machine.InitGDT();
	machine.LoadGDT();

	//init idt
	machine.InitIDT();	
	machine.LoadIDT();

	//init page protection
	machine.InitPageDirectory();
	machine.EnablePageProtection();
	/* 
	 * InitPageDirectory()中将线性地址0-4M映射到物理内存
	 * 0-4M是为保证此注释以下至本函数结尾的代码正确执行！
	 */

	//使用0x10段寄存器
	__asm__ __volatile__
		(" \
		mov $0x10, %ax\n\t \
		mov %ax, %ds\n\t \
		mov %ax, %ss\n\t \
		mov %ax, %es\n\t"
		);

	//将初始化堆栈设置为0xc0400000，这里破坏了封装性，考虑使用更好的方法
	__asm__ __volatile__
		(
		" \
		mov $0xc0400000, %ebp \n\t \
		mov $0xc0400000, %esp \n\t \
		jmp $0x8, $next"
		);			
	
}

/* 应用程序从main返回，进程就终止了，这全是runtime()的功劳。没有它，就只能用exit终止进程了。xV6没这个功能^-^ */
extern "C" void runtime()
{
	/*
	1. 销毁runtime的stack Frame
	2. esp中指向用户栈中argc位置，而ebp尚未正确初始化
	3. eax中存放可执行程序EntryPoint
	4~6. exit(0)结束进程
	*/
	__asm("	leave;	\
			movl %%esp, %%ebp;	\
			call *%%eax;		\
			movl $1, %%eax;	\
			movl $0, %%ebx;	\
			int $0x80"::);
}

/*
  * 1#进程在执行完MoveToUserStack()从ring0退出到ring3优先级后，会调用ExecShell()，此函数通过"int $0x80"
  * (EAX=execv系统调用号)加载“/Shell.exe”程序，其功能相当于在用户程序中执行系统调用execv(char* pathname, char* argv[])。
  */
extern "C" void ExecShell()
{
	int argc = 0;
	char* argv = NULL;
	const char* pathname = "/Shell.exe";

	__asm ("int $0x80"::"a"(11/* execv */),"b"(pathname),"c"(argc),"d"(argv));
	return;
}

extern "C" void next()
{
	
#ifdef USE_VESA
	    intptr_t vesaModeInfoAddr = Machine::KERNEL_SPACE_START_ADDRESS + 0x7e00;
		auto& vesaModeInfo = * (video::svga::VbeModeInfo*) vesaModeInfoAddr;
		video::svga::init(&vesaModeInfo);

		Machine::Instance().InitVESAMemoryMap(
			vesaModeInfo.framebuffer,
			video::svga::VESA_SCREEN_VADDR,
			video::svga::bytesPerPixel * vesaModeInfo.height * vesaModeInfo.width
		);

		video::console::init();
		video::console::writeOutput("VESA enabled.\n", -1, 0xfeba07);
	
#endif
	
	//这个时候0M-4M的内存映射已经不被使用了，所以要重新映射用户态的页表，为用户态程序运行做好准备
	Machine::Instance().InitUserPageTable();
	FlushPageDirectory();
	Machine::Instance().LoadTaskRegister();
	
	/* 获取CMOS当前时间，设置系统时钟 */
	struct SystemTime cTime;
	CMOSTime::ReadCMOSTime(&cTime);
	/* MakeKernelTime()计算出内核时间，从1970年1月1日0时至当前的秒数 */
	Time::time = Utility::MakeKernelTime(&cTime);

	/* 从CMOS中获取物理内存大小 */
	unsigned short memSize = 0;	/* size in KB */
	unsigned char lowMem, highMem;

	/* 这里只是借用CMOSTime类中的ReadCMOSByte函数读取CMOS中物理内存大小信息 */
	lowMem = CMOSTime::ReadCMOSByte(CMOSTime::EXTENDED_MEMORY_ABOVE_1MB_LOW);
	highMem = CMOSTime::ReadCMOSByte(CMOSTime::EXTENDED_MEMORY_ABOVE_1MB_HIGH);
	memSize = (highMem << 8) + lowMem;

	/* 加上1MB以下物理内存区域，计算总内存容量，以字节为单位的内存大小 */
	memSize += 1024; /* KB */
	PageManager::PHY_MEM_SIZE = memSize * 1024;
	UserPageManager::USER_PAGE_POOL_SIZE = PageManager::PHY_MEM_SIZE - UserPageManager::USER_PAGE_POOL_START_ADDR;

	/* 真正操作系统内核初始化逻辑	 */
	Kernel::Instance().Initialize();	
	Kernel::Instance().GetProcessManager().SetupProcessZero();
	isInit = true;

	Kernel::Instance().GetFileSystem().LoadSuperBlock();
	Diagnose::Write("Unix V6++ FileSystem Loaded......OK\n");

	/* 初始化rootDirInode和用户当前工作目录，以便NameI()正常工作 */
	FileManager& fileMgr = Kernel::Instance().GetFileManager();

	fileMgr.rootDirInode = g_InodeTable.IGet(DeviceManager::ROOTDEV, 1);
	fileMgr.rootDirInode->i_flag &= (~Inode::ILOCK);

	User& us = Kernel::Instance().GetUser();
	us.u_cdir = g_InodeTable.IGet(DeviceManager::ROOTDEV, 1);
	us.u_cdir->i_flag &= (~Inode::ILOCK);
	strcpy(us.u_curdir, "/");

	/* 打开TTy设备 */
	int fd_tty = lib_open("/dev/tty1", File::FREAD);

	if ( fd_tty != 0 )
	{
		Utility::Panic("STDIN Error!");
	}
	fd_tty = lib_open("/dev/tty1", File::FWRITE);
	if ( fd_tty != 1 )
	{
		Utility::Panic("STDOUT Error!");
	}
	Diagnose::TraceOn();


#ifdef ENABLE_SPLASH
	// show splash.
	splash();
#endif

	unsigned char* runtimeSrc = (unsigned char*)runtime;
	unsigned char* runtimeDst = 0x00000000;
	for (unsigned int i = 0; i < (unsigned long)ExecShell - (unsigned long)runtime; i++)
	{
		*runtimeDst++ = *runtimeSrc++;
	}

    //us.u_MemoryDescriptor.Release();

	int pid = Kernel::Instance().GetProcessManager().NewProc();         /* 0#进程创建1#进程 */
	
	if( 0 == pid )     
	{
		/* 0#进程执行Sched()，成为系统中永远运行在核心态的唯一进程  */
		us.u_procp->p_ttyp = NULL;
		Kernel::Instance().GetProcessManager().Sched();
	}
	else               
	{
		/* 1#进程执行应用程序shell.exe,是普通进程  */
		Machine::Instance().InitUserPageTable();      //这是直接写0x202,0x203页表，没相对虚实地址映射表一样okay！
		FlushPageDirectory();

		CRT::ClearScreen();

		/* 1#进程回用户态，执行exec("shell.exe")系统调用*/
		MoveToUserStack();
		
		__asm ("call *%%eax" :: "a"((unsigned long)ExecShell - 0xC0000000));   //要访问用户栈，所以一定要有映射！
	}
}


extern "C" void kernelBridge() {  // called by sector2.asm
	initBss();
	callCtors();
	main0();
	callDtors();
}

