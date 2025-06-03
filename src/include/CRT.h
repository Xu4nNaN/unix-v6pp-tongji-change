#ifndef CRT_H
#define CRT_H

#include "TTy.h"

class CRT
{
	/* Const Member */
public:
	/* 显示控制寄存器I/O端口地址 */
	static const unsigned short VIDEO_ADDR_PORT = 0x3d4;	/* 显示控制索引寄存器端口号 */
	static const unsigned short VIDEO_DATA_PORT = 0x3d5;	/* 显示控制数据寄存器端口号 */
	
	/* 屏幕大小为80 * 25 */
	static const unsigned int COLUMNS = 80;
	static unsigned int ROWS;
	
	static const unsigned short COLOR = 0x0F00;		/* char in white color */

	/* Functions */
public:
	/* 将输出缓存队列中的内容输出到屏幕上 */
	static void CRTStart(TTy* pTTy);

protected:
	/* 改变光标位置 */
	static void MoveCursor(unsigned int x, unsigned int y);

	/* 换行处理子程序 */
	static void NextLine();

	/* 退格处理子程序 */
	static void BackSpace();
	
	/* Tab处理子程序 */
	static void Tab();

	/* 显示单个字符 */
	static void WriteChar(char ch);

public:
	/* 清除屏幕 */
	static void ClearScreen();

	/* Members */
public:
	static unsigned short* m_VideoMemory;
	static unsigned int m_CursorX;
	static unsigned int m_CursorY;

	/* 指向输出缓存队列中当前要输出的字符 */
	static char* m_Position;
	/* 指向输出缓存队列中未确认的输出字符的开始处，即可以通过Backspace键删除的内容，
	 * 最后一个回车之前的内容为已确认内容，不可被Backspace键删除。
	 */
	static char* m_BeginChar;
};

#endif
