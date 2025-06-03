#include "TTy.h"
#include "Assembly.h"
#include "Kernel.h"
#include "CRT.h"

/*==============================class TTy_Queue===============================*/
TTy_Queue::TTy_Queue()
{
	this->m_Head = 0;
	this->m_Tail = 0;
}

TTy_Queue::~TTy_Queue()
{
	//nothing to do here
}

char TTy_Queue::GetChar()
{
	char ch = TTy::GET_ERROR;
	{
		if ( this->m_Head == this->m_Tail )
		{
			//Buffer Empty
			return ch;
		}
	}

	ch = this->m_CharBuf[m_Tail];
	this->m_Tail = ( this->m_Tail + 1 ) % TTY_BUF_SIZE;

	return ch;
}

void TTy_Queue::PutChar(char ch)
{
	this->m_CharBuf[m_Head] = ch;
	this->m_Head = ( this->m_Head + 1 ) % TTY_BUF_SIZE;
}

int TTy_Queue::CharNum()
{
	/* 当Head < Tail时使用%运算会出问题！！改用'&'运算。
	 *  譬如下标Head = 5，Tail = 10； (5 - 10) %  TTY_BUF_SIZE ，
	 *  会被当作0xFFFF FFFB  ( =4294967291) 去模运算TTY_BUF_SIZE，结果就错了。
	 */
	// unsigned int ans = this->m_Head - this->m_Tail;
	// ans = ans % TTY_BUF_SIZE;
	// return ans;
	
	int ans = (this->m_Head - this->m_Tail) & (TTy_Queue::TTY_BUF_SIZE - 1);
	return ans;
}

char* TTy_Queue::CurrentChar()
{
	/* 返回下一个将要取出字符的地址 */
	return &this->m_CharBuf[m_Tail];
}

/*==============================class TTy===============================*/
/* 控制台对象实例的定义 */
TTy g_TTy;

TTy::TTy()
{

}

TTy::~TTy()
{

}

/*
 * 从标准输入队列取字符，送用户区
 * 直至队列为空，或满足应用程序之所需（u.u_IOParam.m_Count 为  0）
 * 其间，进程有可能入睡。这一定是 因为标准输入队列为空，且原始输入队列中没有定界符
 * （用户没有输入回车，还可能对原始队列中的输入数据进行修改）
 * */
void TTy::TTRead()
{
	/* 设备没有开始工作，返回 */
	if ( (this->t_state & TTy::CARR_ON) == 0 )
	{
		return;
	}

	if ( this->t_canq.CharNum() || this->Canon() )
	{
		while ( this->t_canq.CharNum() && (this->PassC(this->t_canq.GetChar()) >= 0) );
	}
}

/*
 * 一次一个字节地将用户区中等待输出的内容送标准输出队列。
 * 若队列满，将其中内容刷新至显存。 输出过程会引发多次刷新。
 * 调整CRT::m_BeginChar令其指向输出队列中存放下一个字符的单元。BackSpace不可以擦除该指针之前的任何字符。
 */
void TTy::TTWrite()
{
	/* 
	 * 因为现在的输出设备是内存，其相应速度相当快，所以
	 * 不需要在结束回显后发送中断，这样的代价非常大。这和
	 * 原来unix v6的有些不同。本来可以在输出字符的过程中关
	 * 中断来防止用户输入但不能被正常删除的bug,但因为这样
	 * 可能会导致时钟中断相应的延迟，这个问题更加严重，因
	 * 此没有这样做。
	 */
	char ch;
	
	 /* 设备没有开始工作，返回 */
	if ( (this->t_state & TTy::CARR_ON) == 0 )
	{
		return;
	}

	while ( (ch = CPass()) > 0 )
	{
		/*输出队列中超过规定字符数，需要赶快显示 */
		if ( this->t_outq.CharNum() > TTy::TTHIWAT)
		{
			this->TTStart();
			/* 重新设置BeginChar指向输出字符缓存队列中，未确认部分的起始处。
			 * 目的在于不允许Backspace键删除写在标准输出上的内容，譬如命令提示符之类。
			 */
			CRT::m_BeginChar = this->t_outq.CurrentChar();
		}
		this->TTyOutput(ch);
	}
	this->TTStart();
	CRT::m_BeginChar = this->t_outq.CurrentChar();
	/* 重设BeginChar为了防止错误删除打印的字符，这里需要清空显示缓存，但会造成前面输入回显
	 * 的字符在被删除时并不能被删掉，但是在实际中已经被删除了。
	 */
}

/* 由中断处理程序调用。参数ch是扫描码转换成的ASCII码。
 * 功能：将ch放入原始输入队列，并对其进行回显 （送标准输出队列，之后立即TTStart送显存）
 * 代码中粗糙的地方：this->t_rawq.PutChar(ch) 之前没有判断原始输入队列有没有满。造成的结果是原始队列中原本已有
 * 的字符被删除。
 * 原始输入队列满只能说明一个问题：没有进程在等待键盘输入。
 * 考虑 改进系统，TTyInput加入以下判断：没有进程睡眠等待键盘输入的时候，不要将ch放入原始队列。
 * */
void TTy::TTyInput(char ch)
{
	if ( (ch &= 0xFF) == '\r' && (this->t_flags & TTy::CRMOD) )
	{
		ch = '\n';
	}

	/* 如果是小写终端 */
	if ( (this->t_flags & TTy::LCASE) && ch >= 'A' && ch <= 'Z' )
	{
		ch += 'a' - 'A';
	}

	/* 将输入字符放入原始字符缓存队列 */
	this->t_rawq.PutChar(ch);

	if ( this->t_flags & TTy::RAW || ch == '\n' || ch == TTy::CEOT )
	{
		Kernel::Instance().GetProcessManager().WakeUpAll((unsigned long)&this->t_rawq);
		this->t_rawq.PutChar(0x7);
		this->t_delct++;
	}

	if ( this->t_flags & TTy::ECHO )
	{
		this->TTyOutput(ch);
		this->TTStart();
	}
}

void TTy::TTyOutput(char ch)
{
	/* 如果输入字符为文件结束符，并且终端工作在原始方式下，则返回 */
	if ( (ch &= 0xFF) == TTy::CEOT && (this->t_flags & TTy::RAW) == 0 )
	{
		return;
	}

	if ( '\n' == ch && (this->t_flags & TTy::CRMOD) )
	{
		this->TTyOutput('\r');
	}

	/* 将字符放入输出字符缓存队列中 */
	if (ch)
	{
		this->t_outq.PutChar(ch);
	}
}

void TTy::TTStart()
{
	CRT::CRTStart(this);
}

void TTy::FlushTTy()
{
	while ( this->t_canq.GetChar() >= 0 );
	while ( this->t_outq.GetChar() >= 0 );
	Kernel::Instance().GetProcessManager().WakeUpAll((unsigned long)&this->t_canq);
	Kernel::Instance().GetProcessManager().WakeUpAll((unsigned long)&this->t_outq);
	
	X86Assembly::CLI();
	while ( this->t_rawq.GetChar() >= 0 );
	this->t_delct = 0;
	X86Assembly::STI();
}

int TTy::Canon()
{
	char* pChar;
	char ch;
	User& u = Kernel::Instance().GetUser();

	X86Assembly::CLI();
	while ( this->t_delct == 0 )
	{
		if ( (this->t_state & TTy::CARR_ON) == 0 )
		{
			/* 设备没有打开，返回 */
			return 0;
		}
		u.u_procp->Sleep((unsigned long)&this->t_rawq, ProcessManager::TTIPRI);
	}
	X86Assembly::STI();

loop:
	pChar = &Canonb[2];
	while ( (ch = this->t_rawq.GetChar()) >= 0 )
	{
		if ( 0x7 == ch )	/* 0x07是每个输入串的分隔符 */
		{
			this->t_delct--;
			break;
		}

		if ( (this->t_flags & TTy::RAW) == 0 )	 /* 如果不是raw模式，需要在遇到转义字符时作相应的变换 */
		{
			if ( pChar[-1] != '\\' )
			{
				if ( ch == this->t_erase ) /* 如果是backspace键*/
				{
					if ( pChar > &Canonb[2] )	/* 直接删除前面的字符 */
					{
						pChar--;
					}
					continue;
				}

				if ( ch == this->t_kill )	/* 是删除一行 */
				{
					/* 从行首重新开始，这里使用loop虽然破坏了程序结构，但的确是简化了许多操作，v6原来就是这样写的 */
					goto loop;
				}

				if ( ch == TTy::CEOT )	/* CEOT == 0x4 */
				{
					/* 遇到文件结束符并不作任何操作，这个是在将输入输出转向时将文件当作终端读写时为了保持一致 */
					continue;
				}
			}
		}
		else
		{
			/* 暂时不处理raw模式下的情况 */
		}

		*pChar++ = ch;	/* 普通字符直接放入工作缓存 */
		if ( pChar >= Canonb + TTy::CANBSIZ )
		{
			break;
		}
	}

	char* pEnd = pChar;
	pChar = &Canonb[2];

	/* 将cook过的字符送到行规则缓存，供ttread读取 */
	while ( pChar < pEnd )
	{
		this->t_canq.PutChar(*pChar++);
	}

	return 1;
}

int TTy::PassC(char ch)
{
	User& u = Kernel::Instance().GetUser();

	/* 将字符送入用户目标区 */
	if ( u.u_IOParam.m_Count > 0 )
	{
		*(u.u_IOParam.m_Base++) = ch;
		//u.u_IOParam.m_Offset++;
		u.u_IOParam.m_Count--;
		return 0;
	}
	return -1;
}

char TTy::CPass()
{
	char ch;
	User& u = Kernel::Instance().GetUser();

	ch = *(u.u_IOParam.m_Base++);
	if ( u.u_IOParam.m_Count > 0 )
	{
		u.u_IOParam.m_Count--;
		//u.u_IOParam.m_Offset++;
		return ch;
	}
	else
	{
		return -1;
	}
}



