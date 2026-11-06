/*********************************************************************
	欢迎您为按键精灵制作插件！
	这是按键精灵的插件模板 091026版

	请编译Release版的插件使用，Debug版的插件会导致按键精灵出现异常错误

  	如果有其他问题，欢迎通过以下方式和我们取得联系
	Email: hi@vrbrothers.com
	论坛: bbs.ajjl.cn

    插件模板更新记录:
	091026
	解决由于MFC的功能不完善导致所有的插件函数都在主线程中执行的问题。
    这个问题会导致插件效率低下，而且可能互相阻塞，甚至阻塞主线程。
*********************************************************************/

/*********************************************************************
 * 以下的内容，一般情况下您不需要看懂，也不需要进行修改
*********************************************************************/
#include "stdafx.h"
#include "CQMPlugin.h"
#include   <direct.h>  
#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

BEGIN_MESSAGE_MAP(CQMPlugin, CCmdTarget)
	//{{AFX_MSG_MAP(CQMPlugin)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

#ifdef _DEBUG
	#pragma comment(lib,"CommonLibD.lib")
#else
	#pragma comment(lib,"CommonLib.lib")
#endif

BEGIN_DISPATCH_MAP(CQMPlugin, CCmdTarget)
	DISP_FUNCTION(CQMPlugin, "Get_Plugin_Description", GetPluginDescription, VT_BSTR, VTS_BSTR)
	DISP_FUNCTION(CQMPlugin, "Get_Plugin_Interpret_Template", GetPluginInterpretTemplate, VT_BSTR, VTS_BSTR)
	//{{AFX_DISPATCH_MAP(CQMPlugin)
	DISP_FUNCTION(CQMPlugin, "WriteINI", WriteINI, VT_I4, VTS_BSTR VTS_BSTR VTS_BSTR VTS_BSTR)
	DISP_FUNCTION(CQMPlugin, "ReadINI", ReadINI, VT_BSTR, VTS_BSTR VTS_BSTR VTS_BSTR)
	DISP_FUNCTION(CQMPlugin, "CopyFile", CopyFile, VT_I4, VTS_BSTR VTS_BSTR)
	DISP_FUNCTION(CQMPlugin, "IsFileExit", IsFileExit, VT_BOOL, VTS_BSTR)
	DISP_FUNCTION(CQMPlugin, "DeleteFile", DeleteFile, VT_I4, VTS_BSTR)
	DISP_FUNCTION(CQMPlugin, "MoveFile", MoveFile, VT_I4, VTS_BSTR VTS_BSTR)
	DISP_FUNCTION(CQMPlugin, "CreateFolder", CreateFolder, VT_I4, VTS_BSTR)
	DISP_FUNCTION(CQMPlugin, "DeleteFolder", DeleteFolder, VT_I4, VTS_BSTR)
	DISP_FUNCTION(CQMPlugin, "ReNameFile", ReNameFile, VT_I4, VTS_BSTR VTS_BSTR)
	DISP_FUNCTION(CQMPlugin, "SetAttrib", SetAttrib, VT_BOOL, VTS_BSTR VTS_BOOL)
	DISP_FUNCTION(CQMPlugin, "SetDate", SetDate, VT_I4, VTS_BSTR VTS_BSTR)
	DISP_FUNCTION(CQMPlugin, "GetFileLength", GetFileLength, VT_I4, VTS_BSTR)
	DISP_FUNCTION(CQMPlugin, "SelectFile", SelectFile, VT_BSTR, VTS_NONE)
	DISP_FUNCTION(CQMPlugin, "SelectDirectory", SelectDirectory, VT_BSTR, VTS_NONE)
	DISP_FUNCTION(CQMPlugin, "SeekFile", SeekFile, VT_I4, VTS_I4 VTS_I4)
	DISP_FUNCTION(CQMPlugin, "ReadLine", ReadLine, VT_BSTR, VTS_I4)
	DISP_FUNCTION(CQMPlugin, "WriteLine", WriteLine, VT_I4, VTS_I4 VTS_BSTR)
	DISP_FUNCTION(CQMPlugin, "ReadFileEx", ReadFileEx, VT_BSTR, VTS_BSTR)
	DISP_FUNCTION(CQMPlugin, "WriteFileEx", WriteFileEx, VT_I4, VTS_BSTR VTS_BSTR)
	DISP_FUNCTION(CQMPlugin, "ReadFile", ReadFile, VT_BSTR, VTS_I4 VTS_I4)
	DISP_FUNCTION(CQMPlugin, "WriteFile", WriteFile, VT_I4, VTS_I4 VTS_BSTR)
	DISP_FUNCTION(CQMPlugin, "ExistFile", ExistFile, VT_I4, VTS_BSTR)
	DISP_FUNCTION(CQMPlugin, "OpenFile", OpenFile, VT_I4, VTS_BSTR)
	DISP_FUNCTION(CQMPlugin, "CloseFile", CloseFile, VT_I4, VTS_I4)
	//}}AFX_DISPATCH_MAP
DISP_FUNCTION(CQMPlugin, "Version", Version, VT_BSTR, VTS_NONE)
	DISP_FUNCTION(CQMPlugin, "Find", Find, VT_BSTR, VTS_I4 VTS_I4 VTS_I4 VTS_I4 VTS_BSTR VTS_R8 VTS_I4 VTS_BSTR VTS_I4)
	DISP_FUNCTION(CQMPlugin, "FindAll", FindAll, VT_BSTR, VTS_I4 VTS_I4 VTS_I4 VTS_I4 VTS_BSTR VTS_R8 VTS_I4 VTS_BSTR VTS_I4)
	DISP_FUNCTION(CQMPlugin, "FindAllPos", FindAllPos, VT_BSTR, VTS_I4 VTS_I4 VTS_I4 VTS_I4 VTS_BSTR VTS_R8 VTS_I4 VTS_BSTR VTS_I4)
	DISP_FUNCTION(CQMPlugin, "FindColorMulti", FindColorMulti, VT_BSTR, VTS_I4 VTS_I4 VTS_I4 VTS_I4 VTS_BSTR VTS_BSTR VTS_R8 VTS_I4 VTS_BSTR VTS_I4)
	DISP_FUNCTION(CQMPlugin, "FindColorMultiPos", FindColorMultiPos, VT_BSTR, VTS_I4 VTS_I4 VTS_I4 VTS_I4 VTS_BSTR VTS_BSTR VTS_R8 VTS_I4 VTS_BSTR VTS_I4)
	DISP_FUNCTION(CQMPlugin, "GetSectorAngle", GetSectorAngle, VT_BSTR, VTS_I4 VTS_I4 VTS_I4 VTS_I4 VTS_I4 VTS_I4 VTS_I4 VTS_BSTR VTS_I4)
	DISP_FUNCTION(CQMPlugin, "GetArrowAngle", GetArrowAngle, VT_BSTR, VTS_I4 VTS_I4 VTS_I4 VTS_R8 VTS_I4 VTS_BSTR)
	DISP_FUNCTION(CQMPlugin, "MoveMouseR", MoveMouseR, VT_BSTR, VTS_I4 VTS_I4 VTS_I4 VTS_I4 VTS_I4)
	DISP_FUNCTION(CQMPlugin, "MoveMouseP", MoveMouseP, VT_BSTR, VTS_I4 VTS_I4 VTS_I4 VTS_I4 VTS_I4)
	DISP_FUNCTION(CQMPlugin, "SoundStart", SoundStart, VT_BSTR, VTS_NONE)
	DISP_FUNCTION(CQMPlugin, "SoundStop", SoundStop, VT_BSTR, VTS_NONE)
	DISP_FUNCTION(CQMPlugin, "SoundWait", SoundWait, VT_BSTR, VTS_I4 VTS_I4)
	DISP_FUNCTION(CQMPlugin, "SoundScore", SoundScore, VT_BSTR, VTS_NONE)
	DISP_FUNCTION(CQMPlugin, "DrawBox", DrawBox, VT_BSTR, VTS_I4 VTS_I4 VTS_I4 VTS_BSTR)
END_DISPATCH_MAP()

IMPLEMENT_OLECREATE(CQMPlugin, "QMPlugin.zaoyo", 0x9A3B7C21, 0x4D5E, 0x4F60, 0x8A, 0x71, 0xB2, 0xC3, 0xD4, 0xE5, 0xF6, 0x07)

BOOL g_bEnablePluginValidate = TRUE;
/*********************************************************************
 * 以上的内容，一般情况下您不需要看懂，也不需要进行修改
*********************************************************************/

/*********************************************************************
	按键精灵的每个插件可以支持若干条命令。
	如果您需要写一条自己的命令，需要遵循以下步骤：
	1. 打开文件CQMPlugin.cpp，就是您现在看到的这个文件
	2. 在Visual C++ 6.0的菜单中选择 View->ClassWizard
	3. 在弹出的对话框中选择Automation面板，按Add Method按钮
	4. 依次填写您的插件函数的名字、返回类型和参数类型
	   需要注意的是，如果返回值是字符串类型，请选择BSTR
	   如果参数是字符串类型，请选择LPCTSTR
	   如果需要传地址的参数（可用于返回多个值），参数类型请选择VARIANT*，如下面的GetScreenResolution插件函数所示
	5. 按OK按钮，您会看到CQMPlugin.cpp有了一个空白的函数，您只需要填写函数功能即可
	6. 您也可以在Automation面板中选择删除某个已有的函数，并同时在下面的程序中删除对应的函数体
	7. 在下面的m_DescriptionList表格中填写您的插件说明和函数说明
	8. 编译形成DLL文件，把DLL文件放在按键精灵的Plugin目录下
	9. 打开按键精灵，进入编辑脚本界面，打开“插件命令”面板，编写脚本测试你的插件！
*********************************************************************/

/*********************************************************************
 * 名称:m_DescriptionList
 * 你可以把它看成一个表格，表格中的每一行代表了一条插件命令。
 * 每行第一个字符串是插件命令的名字（如果为空，则代表整个插件）；第二个字符串是这条插件命令的解释；
 * 第三个字符串是插件命令的显示模板信息，其中的$1 $2 ... 等内容再显示时将用第一个参数、第二个参数、... 代替
 * 当你需要新增一条插件命令的时候，就需要在表格里面加一行。
*********************************************************************/
CQMPlugin::DESCRIPTION_LIST CQMPlugin::m_DescriptionList[] = 
{
	{"","早柚插件（找图/找色/测角/鼠标，抗渲染色差）",""},
	{"Find","找图(左,上,右,下,图片,相似度,绘制开关,颜色,方向)[x|y|实际相似度]","在$1,$2,$3,$4范围内找图$5，相似度$6"},
	{"FindAll","多图同时查找，全部找到才成功(左,上,右,下,图片列表,相似度,绘制开关,颜色,方向)[各图x|y|相似度;中心点]","在$1,$2,$3,$4范围内找多图$5，相似度$6"},
	{"FindAllPos","找一张图的所有匹配位置(左,上,右,下,图片,相似度,绘制开关,颜色,方向)[数量;x|y|相似度...]","找$5在$1,$2,$3,$4内的所有位置，相似度$6"},
	{"FindColorMulti","多点找色(左,上,右,下,主色,偏色点,相似度,绘制开关,颜色,方向)[x|y|实际相似度]","在$1,$2,$3,$4内按主色$5和多点$6找色，相似度$7"},
	{"FindColorMultiPos","多点找色的所有位置(左,上,右,下,主色,偏色点,相似度,绘制开关,颜色,方向)[数量;x|y|相似度...]","找主色$5和多点$6在$1,$2,$3,$4内的所有位置，相似度$7"},
	{"GetSectorAngle","扇形角度(圆心X,圆心Y,内半径,外半径,灵敏度,校准,绘制开关,颜色,方法)[角度|集中度|覆盖]","测以$1,$2为圆心、$3~$4环带的扇形角度，灵敏度$5"},
	{"GetArrowAngle","箭头朝向(圆心X,圆心Y,半径,基准角,绘制开关,颜色)[角度|像素数|长度]","测$1,$2附近$3像素内的箭头朝向，减基准角$4"},
	{"MoveMouseR","相对移动鼠标-按计数(dx,dy,步数,间隔,方法)[ok|步数|dx|dy]","相对移动鼠标 $1,$2 计数；步数$3 间隔$4 方法$5"},
	{"MoveMouseP","相对移动鼠标-按像素(dx,dy,步数,间隔,方法)[ok|步数|dx|dy|计数]","相对移动鼠标 $1,$2 像素；步数$3 间隔$4 方法$5"},
	{"SoundStart","开始监听游戏音效(阈值已内置)[ok|设备Hz|声道]/fail|错误码","开始监听游戏音效（默认播放设备环回采集），阈值已内置：闪避0.35、反击0.25"},
{"SoundStop","停止音效监听()[ok]","停止音效监听并释放采集设备"},
{"SoundWait","等待音效命中(超时毫秒,有效期毫秒)[2=闪避|3=反击|0=超时|-1=未启动]","等待音效命中，最多$1毫秒；结果保留$2毫秒（0=不限），过时丢弃"},
{"SoundScore","当前音效分数()[闪避分|反击分]","读取当前两个音效的匹配分数（调试用）"},
{"DrawBox","画一个正方形框并1.5秒后消失(中心X,中心Y,边长,颜色)[ok|左|上|右|下]","在$1,$2处画一个边长$3的正方形框，颜色$4，1.5秒后自动消失"},
{"Version","插件版本号",""},
};
//这行您不需要看懂，也不需要修改
const int CQMPlugin::m_iDescriptionListSize = sizeof(CQMPlugin::m_DescriptionList) / sizeof(CQMPlugin::DESCRIPTION_LIST);



/*********************************************************************
 * 下面是公共处理函数。
 * OnLoad 函数会在插件被加载时调用，返回值通常为TRUE。如果返回FALSE，表示插件拒绝被加载
 * OnExit 函数会在插件被卸载时调用，没有返回值
*********************************************************************/
BOOL OnLoad(HMODULE hModule, int iReserved)
{
	return TRUE;
}

void OnExit()
{
	return;
}

//向配置文件里写一个键值(小节名,键名,写入内容,INI文件路径)[返回操作结果，不为0表示成功，0表示失败]
long CQMPlugin::WriteINI(LPCTSTR lpszSectionName, LPCTSTR lpszKeyName, LPCTSTR lpszBuffer, LPCTSTR lpszINIFilePath) 
{
	return WritePrivateProfileString(lpszSectionName,lpszKeyName,lpszBuffer,lpszINIFilePath);//向配置文件中写入一个键值
}

//从配置文件里读一个键值(小节名,键名,INI文件路径)[返回读出内容]
BSTR CQMPlugin::ReadINI(LPCTSTR lpszSectionName, LPCTSTR lpszKeyName, LPCTSTR lpszINIFilePath) 
{
	CString strResult;
	char szBuffer[4096] = {0};	
	GetPrivateProfileString(lpszSectionName,lpszKeyName,"",szBuffer,sizeof(szBuffer),lpszINIFilePath);//从配置文件中读取一个键值
	strResult = szBuffer;
	return strResult.AllocSysString();
}

//拷贝文件(目标文件,存放路径)[返回操作结果，不为0表示成功，0表示失败]
long CQMPlugin::CopyFile(LPCTSTR lpszSourceFile, LPCTSTR lpszTargetFile) 
{
	return ::CopyFile(lpszSourceFile,lpszTargetFile,FALSE);
}

//判断一个文件是否存在(目标文件)[返回操作结果，1表示文件存在，0表示文件不存在]
BOOL CQMPlugin::IsFileExit(LPCTSTR lpszSourceFile) 
{
    DWORD dwFileAttr = GetFileAttributes(lpszSourceFile);//只有一个字符串参数，可以不用分割参数了

	if((int)dwFileAttr < 0)							//出错或者没有找到
	{
		return FALSE;
	}
	else if(dwFileAttr & FILE_ATTRIBUTE_DIRECTORY)	//是文件夹
	{
		return FALSE;
	}
	else											//是文件
	{
		return TRUE;
	}
}

//删除文件(目标文件)[返回操作结果，不为0表示成功，0表示失败]
long CQMPlugin::DeleteFile(LPCTSTR lpszFilePath) 
{
	return ::DeleteFile(lpszFilePath);
}

//移动文件(目标文件,存放路径)[返回操作结果，不为0表示成功，0表示失败]
long CQMPlugin::MoveFile(LPCTSTR lpszSourceFile, LPCTSTR lpszTargetFile) 
{
	return ::MoveFile(lpszSourceFile,lpszTargetFile);
}

//建立文件夹(创建文件夹路径)[返回操作结果，0表示成功，-1表示失败]
long CQMPlugin::CreateFolder(LPCTSTR lpszDirectory) 
{
	return _mkdir(lpszDirectory); 
}

//删除文件夹(目标文件)[返回操作结果，不为0表示成功，0表示失败]
long CQMPlugin::DeleteFolder(LPCTSTR lpszDirectory) 
{
	SHFILEOPSTRUCT FileOp;
	FileOp.fFlags = FOF_SILENT | FOF_NOCONFIRMATION | FOF_NOERRORUI;//设定标志：不显示提示对话框
	FileOp.pFrom = lpszDirectory;									//要删除的目录名，相对的，绝对的都可以
	FileOp.wFunc = FO_DELETE;										//指定操作为删除
	if (SHFileOperation(&FileOp) != 0)								//开始调用Shell的文件操作...如果返回0表示操作成功，不为0表示失败
	{
		return 0;
	}
	return 1;
}

//重命名文件(原文件名,新文件名)[返回操作结果，不为0表示成功，0表示失败]
long CQMPlugin::ReNameFile(LPCTSTR lpszOldName,LPCTSTR lpszNewName) 
{
	return rename(lpszOldName,lpszNewName);
}

//设置文件属性(目标文件,读/写)[返回操作结果，不为0表示成功，0表示失败]
BOOL CQMPlugin::SetAttrib(LPCTSTR lpszFilePath, BOOL bSign) 
{
	CFileStatus rStatus;
	CFile::GetStatus(lpszFilePath,rStatus);				//获得文件的属性设置
	if (bSign==0)
	{
		rStatus.m_attribute=rStatus.m_attribute & 0x3E;	//如果文件为只读的，将只读属性去掉
	}
	else
	{
		rStatus.m_attribute=rStatus.m_attribute | 0x01;	//如果文件为读的，将只读属性钩上
	}
	CFile::SetStatus(lpszFilePath, rStatus );			//更改文件的属性设置
	return TRUE;
}

//设置文件日期和时间(目标文件,日期和时间)[返回操作结果，不为0表示成功，0表示失败]
long CQMPlugin::SetDate(LPCTSTR lpszFilePath, LPCTSTR lpszLastWriteTime) 
{
	// TODO: Add your control notification handler code here
	int iYear,iMonth,iDay,iHour,iMinute,iSecond;
	//分隔字符串，如果分隔的元素个数和返回值不同，表示分隔失败
	if (sscanf(lpszLastWriteTime,"%d-%d-%d %d:%d:%d",&iYear,&iMonth,&iDay,&iHour,&iMinute,&iSecond) != 6)
	{
		return 0;
	}
	SYSTEMTIME systime;
	systime.wYear = iYear;
	systime.wMonth = iMonth;
	systime.wDay = iDay;
	systime.wHour = iHour;
	systime.wMinute = iMinute;
	systime.wDayOfWeek = 0;
	systime.wMilliseconds = 0;
	systime.wSecond = iSecond;
	FILETIME lastWriteTime;
	if (!SystemTimeToFileTime(&systime,&lastWriteTime))							//将系统时间转化为文件时间格式，如果转换失败，则返回0
	{
		return 0;
	}
	char szbuffer[1024]={0};
	HFILE hHandle = ::OpenFile(lpszFilePath,(LPOFSTRUCT)szbuffer,OF_WRITE);		//打开目标文件
	if(hHandle == HFILE_ERROR)													//打开目标文件失败
	{
		return 0;
	}
	SetFileTime((HANDLE)hHandle,&lastWriteTime,&lastWriteTime,&lastWriteTime);	//设置文件日期和时间
	_lclose(hHandle);															//关闭目标文件
	return 1;
}

//得到文本内容长度(目标文件)[内容长度]
 long CQMPlugin::GetFileLength(LPCTSTR lpszFilePath) 
 {
 	CFile fTargetFile;
 	if (fTargetFile.Open(lpszFilePath,CFile::modeRead | CFile::typeBinary) == TRUE)	//打开目标文件成功
 	{
		DWORD dwFilelength = fTargetFile.GetLength();								//获取文件长度
 		fTargetFile.Close();														//关闭目标文件
 		return (long)dwFilelength;
 	}
	else																			//打开目标文件失败
	{
		return 0;
	}
 	
 }

//弹出选择文件的对话框()[返回用户选择的文件路径]
BSTR CQMPlugin::SelectFile() 
{
	CString strResult;
	char szBuffer[4096] = {0};
	OPENFILENAME sOpenFileDialogInfo = {0};
	sOpenFileDialogInfo.lStructSize = sizeof(sOpenFileDialogInfo);
	sOpenFileDialogInfo.lpstrFile = szBuffer;
	sOpenFileDialogInfo.nMaxFile = sizeof(szBuffer);
	sOpenFileDialogInfo.Flags = 6148;
	if(GetOpenFileName(&sOpenFileDialogInfo) == 0)//弹出选择文件对话框
	{
		return NULL;
	}
	strResult = szBuffer;
	return strResult.AllocSysString();
}

//弹出选择文件夹的对话框()[返回用户选择的文件夹路径]
BSTR CQMPlugin::SelectDirectory() 
{
	CString strResult;
	char szPath[4096] = {0};
	BROWSEINFO sBrowseDirectoryInfo = {0};
	LPITEMIDLIST lpSelectDirectoryReturn = NULL;
	sBrowseDirectoryInfo.lpszTitle = "";
	sBrowseDirectoryInfo.ulFlags = BIF_RETURNONLYFSDIRS;
    
	lpSelectDirectoryReturn = SHBrowseForFolder(&sBrowseDirectoryInfo);//弹出选择文件夹对话框
	
	if(lpSelectDirectoryReturn == NULL)//没有作出选择(如选择了“取消”)
	{
		return NULL;
	}
	SHGetPathFromIDList(lpSelectDirectoryReturn,szPath);//将列表格式转换为路径
    strResult = szPath;
	return strResult.AllocSysString();
}

//设置文件的当前读写位置(目标文件句柄，设置偏移量)[返回操作结果，不为0表示成功，0表示失败]
long CQMPlugin::SeekFile(long lHandle, long lOffset) 
{
	if (SetFilePointer((HANDLE)lHandle,lOffset,NULL,FILE_BEGIN) == 0xFFFFFFFF)//设置失败
	{
		return 0;
	}
	return 1;
}

//从文件当前的读写位置读一行内容(目标文件句柄)[返回文本内容]
BSTR CQMPlugin::ReadLine(long lHandle) 
{
	CString strResult;
	DWORD dwNumberOfBytesRead;
	char szCurrentRead[4096] = {0};
	
	int i = 0;
	do 
	{
		::ReadFile((HANDLE)lHandle,szCurrentRead+i,1,&dwNumberOfBytesRead,NULL);//从目标文件当前位置读取指定长度内容
		if(*(szCurrentRead+i) == '\r')	//读到行末
			break;
		i++;
	} while(dwNumberOfBytesRead);
	*(szCurrentRead+i)= '\0';			//加上字符串结束标示
	strResult = szCurrentRead;
	return strResult.AllocSysString();
}

//从文件当前的读写位置写入一行内容(目标文件句柄，写入文本内容)[返回操作结果，不为0表示成功，0表示失败]
long CQMPlugin::WriteLine(long lHandle, LPCTSTR lpszWriteContent) 
{
	DWORD dwNumberOfBytesWrite;
	strcat((char*)lpszWriteContent,"\r\n");//和WriteFile的唯一区别是后面多了回车换行符
	return ::WriteFile((HANDLE)lHandle,lpszWriteContent,strlen(lpszWriteContent),&dwNumberOfBytesWrite,NULL);//在文件当前位置写入指定长度文本内容
}

//读文本内容(目标文件路径)[返回文本内容]
BSTR CQMPlugin::ReadFileEx(LPCTSTR lpszFilePath) 
{
	CStdioFile fTargetFile;
	CString strResult = "";
	CString strReadFileString;
	
	if(fTargetFile.Open(lpszFilePath, CFile::modeRead) == TRUE)		//打开文件成功
	{     
		while(fTargetFile.ReadString(strReadFileString) != FALSE)	//读取一行文本
		{     
			strResult += strReadFileString + "\r\n";
		}
	}     
	else															//打开文件失败
	{
		return NULL;
	}
	fTargetFile.Close();											//关闭文件
	return strResult.AllocSysString();
}

//写文本内容(目标文件,写入内容)[返回操作结果，不为0表示成功，0表示失败]
long CQMPlugin::WriteFileEx(LPCTSTR lpszFilePath, LPCTSTR lpszWriteContent) 
{
	CStdioFile fTargetFile;
	CString strWriteFileString;
	if(fTargetFile.Open(lpszFilePath,CFile::modeCreate|CFile::modeWrite) == TRUE)	//打开目标文件
    {    
		fTargetFile.SeekToEnd();													//搜索到文件结束位置
	}
	else																			//打开目标文件失败
	{
		return 0;
	}
	strWriteFileString = lpszWriteContent;
	fTargetFile.WriteString(strWriteFileString+"\r\n");								//写入文本
	fTargetFile.Close();															//关闭文件
	return 1;
}

//从文件当前的读写位置读取一定长度的内容(目标文件句柄，读取文本长度)[文本内容]
BSTR CQMPlugin::ReadFile(long lHandle, long lReadLength) 
{
	CString strResult;
	DWORD dwNumberOfBytesRead;
	char szBuffer[4096] = {0};
	::ReadFile((HANDLE)lHandle,szBuffer,lReadLength,&dwNumberOfBytesRead,NULL);	//从目标文件当前位置读取指定长度内容
	szBuffer[dwNumberOfBytesRead] = '\0';										//加上字符串结束标示
	strResult = szBuffer;
	return strResult.AllocSysString();
}

//往文件当前读写位置写入一个字符串(目标文件句柄，写入文本内容)[返回操作结果，不为0表示成功，0表示失败]
long CQMPlugin::WriteFile(long lHandle, LPCTSTR lpszWriteContent) 
{
	DWORD dwNumberOfBytesWrite;//获取成功写入文本长度
	return ::WriteFile((HANDLE)lHandle,lpszWriteContent,strlen(lpszWriteContent),&dwNumberOfBytesWrite,NULL);//在文件当前位置写入指定长度文本内容
}

//判断文件或文件夹是否存在(目标文件)[返回操作结果，不为0表示成功，0表示失败]
long CQMPlugin::ExistFile(LPCTSTR lpszFilePath) 
{
	DWORD dwFileAttr = GetFileAttributes(lpszFilePath);	//只有一个字符串参数，可以不用分割参数了
	
	if((int)dwFileAttr < 0)								//出错或者没有找到
	{
		return 0;
	}
	else if(dwFileAttr & FILE_ATTRIBUTE_DIRECTORY)		//是文件夹
	{
		return 2;
	}
	else												//是文件
	{
		return 1;
	}
}

//打开一个文件(目标文件)[返回目标文件句柄,0表示打开文件失败]
long CQMPlugin::OpenFile(LPCTSTR lpszFilePath) 
{
	return (long)CreateFile(lpszFilePath,GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,0);//打开一个文件，如果文件不存在，则先创建文件
}

//关闭一个已经打开的文件(目标文件句柄)[返回操作结果，不为0表示成功，0表示失败]
long CQMPlugin::CloseFile(long lHandle) 
{
	return CloseHandle((HANDLE)lHandle);//关闭指定句柄
}

// Version() 的实现移到 zaoyo_shell.cpp：转发引擎的 DISPID 10，报出来的就是引擎版本。
// （原来这里硬编码 2.8.0，Version() 永远显示 2.8.0，排查时会被误导。）