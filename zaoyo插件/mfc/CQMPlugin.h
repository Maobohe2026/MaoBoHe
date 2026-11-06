#if !defined(AFX_CQMPLUGIN_H__8D2C77FD_C8DC_41F9_99CC_03084B9C4310__INCLUDED_)
#define AFX_CQMPLUGIN_H__8D2C77FD_C8DC_41F9_99CC_03084B9C4310__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
// CQMPlugin.h : header file
//

/////////////////////////////////////////////////////////////////////////////
// CQMPlugin command target

class CQMPlugin : public CCmdTarget
{
	DECLARE_DYNCREATE(CQMPlugin)

	CQMPlugin();           // protected constructor used by dynamic creation

// Attributes
public:
	struct DESCRIPTION_LIST
	{
		char *m_lpszItemName;
		char *m_lpszDescription;
		char *m_lpszInterpretTemplate;
	};

// Operations
public:

// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CQMPlugin)
	public:
	virtual void OnFinalRelease();
	//}}AFX_VIRTUAL

// Implementation
protected:
	virtual ~CQMPlugin();
	afx_msg BSTR GetPluginDescription(LPCTSTR bstrItemName);
	afx_msg BSTR GetPluginInterpretTemplate(LPCTSTR bstrItemName);

	// Generated message map functions
	//{{AFX_MSG(CQMPlugin)
		// NOTE - the ClassWizard will add and remove member functions here.
	//}}AFX_MSG

	DECLARE_OLECREATE(CQMPlugin)
	DECLARE_MESSAGE_MAP()

	// Generated OLE dispatch map functions
	//{{AFX_DISPATCH(CQMPlugin)
	afx_msg long WriteINI(LPCTSTR lpszSectionName, LPCTSTR lpszKeyName, LPCTSTR lpszBuffer, LPCTSTR lpszINIFilePath);
	afx_msg BSTR ReadINI(LPCTSTR lpszSectionName, LPCTSTR lpszKeyName, LPCTSTR lpszINIFilePath);
	afx_msg long CopyFile(LPCTSTR lpszSourceFile, LPCTSTR lpszTargetFile);
	afx_msg BOOL IsFileExit(LPCTSTR lpszSourceFile);
	afx_msg long DeleteFile(LPCTSTR lpszFilePath);
	afx_msg long MoveFile(LPCTSTR lpszSourceFile, LPCTSTR lpszTargetFile);
	afx_msg long CreateFolder(LPCTSTR lpszDirectory);
	afx_msg long DeleteFolder(LPCTSTR lpszDirectory);
	afx_msg long ReNameFile(LPCTSTR lpszOldName,LPCTSTR lpszNewName);
	afx_msg BOOL SetAttrib(LPCTSTR lpszFilePath, BOOL bSign);
	afx_msg long SetDate(LPCTSTR lpszFilePath, LPCTSTR lpszLastWriteTime);
	afx_msg long GetFileLength(LPCTSTR lpszFilePath);
	afx_msg BSTR SelectFile();
	afx_msg BSTR SelectDirectory();
	afx_msg long SeekFile(long lHandle, long lOffset);
	afx_msg BSTR ReadLine(long lHandle);
	afx_msg long WriteLine(long lHandle, LPCTSTR lpszWriteContent);
	afx_msg BSTR ReadFileEx(LPCTSTR lpszFilePath);
	afx_msg long WriteFileEx(LPCTSTR lpszFilePath, LPCTSTR lpszWriteContent);
	afx_msg BSTR ReadFile(long lHandle, long lReadLength);
	afx_msg long WriteFile(long lHandle, LPCTSTR lpszWriteContent);
	afx_msg long ExistFile(LPCTSTR lpszFilePath);
	afx_msg long OpenFile(LPCTSTR lpszFilePath);
	afx_msg long CloseFile(long lHandle);
	afx_msg BSTR Version();
	afx_msg BSTR Find(long a1, long a2, long a3, long a4, LPCTSTR s1, double d1, long a5, LPCTSTR s2, long a6);
	afx_msg BSTR FindAll(long a1, long a2, long a3, long a4, LPCTSTR s1, double d1, long a5, LPCTSTR s2, long a6);
	afx_msg BSTR FindAllPos(long a1, long a2, long a3, long a4, LPCTSTR s1, double d1, long a5, LPCTSTR s2, long a6);
	afx_msg BSTR FindColorMulti(long a1, long a2, long a3, long a4, LPCTSTR s1, LPCTSTR s2, double d1, long a5, LPCTSTR s3, long a6);
	afx_msg BSTR FindColorMultiPos(long a1, long a2, long a3, long a4, LPCTSTR s1, LPCTSTR s2, double d1, long a5, LPCTSTR s3, long a6);
	afx_msg BSTR GetSectorAngle(long a1, long a2, long a3, long a4, long a5, long a6, long a7, LPCTSTR s1, long a8);
	afx_msg BSTR GetArrowAngle(long a1, long a2, long a3, double d1, long a4, LPCTSTR s1);
	afx_msg BSTR MoveMouseR(long a1, long a2, long a3, long a4, long a5);
	afx_msg BSTR MoveMouseP(long a1, long a2, long a3, long a4, long a5);
	afx_msg BSTR SoundStart();
	afx_msg BSTR SoundStop();
	afx_msg BSTR SoundWait(long lTimeout, long lMaxAge);
	afx_msg BSTR SoundScore();
	afx_msg BSTR DrawBox(long lCenterX, long lCenterY, long lSize, LPCTSTR sColor);
	//}}AFX_DISPATCH
	DECLARE_DISPATCH_MAP()
	DECLARE_INTERFACE_MAP()

private:
	static DESCRIPTION_LIST m_DescriptionList[];
	static const int m_iDescriptionListSize;
};

/////////////////////////////////////////////////////////////////////////////

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_CQMPLUGIN_H__8D2C77FD_C8DC_41F9_99CC_03084B9C4310__INCLUDED_)
