#include "global.hpp"
#include "game_config.hpp"
#include "loadscreen.hpp"
#include "editor.hpp"
#include "stdafx.h"
#include <windowsx.h>

#include "resource.h"
#include "struct.h"
// #include "param_def.h"

#include "xfunc.h"
#include "win32x.h"

#include <string>
#include <vector>

extern editor editor_;

static void OnDeleteBt(HWND hdlgP, char *fname);
BOOL extra_kingdom_ins_disk(char* kingdom_src, char* kingdom_ins, char* kingdon_ins_android);

void create_ddesc_toolinfo(HWND hwndP)
{
	TOOLINFO	ti;
    RECT		rect;

	// CREATE A TOOLTIP WINDOW for OK
	if (gdmgr._tt_refresh) {
		DestroyWindow(gdmgr._tt_refresh);
	}
	gdmgr._tt_refresh = CreateWindowEx(WS_EX_TOPMOST,
        TOOLTIPS_CLASS,
        NULL,
        WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,		
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        GetDlgItem(hwndP, IDC_ST_MEDIA),
        NULL,
        gdmgr._hinst,
        NULL
        );
	// SetWindowPos(hwndTT, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    // GET COORDINATES OF THE MAIN CLIENT AREA
    GetClientRect(GetDlgItem(hwndP, IDC_ST_MEDIA), &rect);
    // INITIALIZE MEMBERS OF THE TOOLINFO STRUCTURE
    ti.cbSize = sizeof(TOOLINFO);
    ti.uFlags = TTF_SUBCLASS;
    ti.hwnd = GetDlgItem(hwndP, IDC_ST_MEDIA);
    ti.hinst = gdmgr._hinst;
    ti.uId = 0;
    ti.lpszText = "Refresh";
    // ToolTip control will cover the whole window
    ti.rect.left = rect.left;    
    ti.rect.top = rect.top;
    ti.rect.right = rect.right;
    ti.rect.bottom = rect.bottom;

    // SEND AN ADDTOOL MESSAGE TO THE TOOLTIP CONTROL WINDOW 
    SendMessage(gdmgr._tt_refresh, TTM_ADDTOOL, 0, (LPARAM) (LPTOOLINFO) &ti);

	return;
}

typedef struct {
	HWND		hctl;
	HTREEITEM	htvi;
	char		curdir[_MAX_PATH];
	uint32_t	maxdeep;	// �����չmaxdeep��, 0: ֻ��չ��Ŀ¼, 1: ��չ����Ŀ¼�µ�Ŀ¼Ϊֹ, -1: ��Ϊ���޷�����,��ζ��ȫ��չ	
	uint32_t	deep;		// 0, 1, 2, ... 
} tv_walk_dir_param_t;

BOOL cb_walk_dir_explorer(char *name, uint32_t flags, uint64_t len, int64_t lastWriteTime, uint32_t* ctx)
{
	int			iimg = select_iimage_according_fname(name, flags);
	HTREEITEM	htvi;
	TVSORTCB	tvscb;
	tv_walk_dir_param_t* wdp = (tv_walk_dir_param_t*)ctx;
	
	htvi = TreeView_AddLeaf(wdp->hctl, wdp->htvi);
	TreeView_SetItem1(wdp->hctl, htvi, TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_PARAM | TVIF_CHILDREN, (LPARAM)htvi, iimg, iimg, 0, name);
	
	tvscb.hParent = wdp->htvi;
	tvscb.lpfnCompare = fn_tvi_compare_sort;
	tvscb.lParam = (LPARAM)wdp->hctl;

	TreeView_SortChildrenCB(wdp->hctl, &tvscb, 0);

	if (flags & FILE_ATTRIBUTE_DIRECTORY ) {
		tv_walk_dir_param_t	wdp2;

		sprintf(wdp2.curdir, "%s\\%s", wdp->curdir, name);

		if (wdp->maxdeep > wdp->deep) {
			tv_walk_dir_param_t	wdp2;

			wdp2.hctl = wdp->hctl;
			wdp2.htvi = htvi;
			wdp2.maxdeep = wdp->maxdeep;
			wdp2.deep = wdp->deep + 1;
			walk_dir_win32_deepen(wdp2.curdir, 0, cb_walk_dir_explorer, (uint32_t *)&wdp2);
		} else if (!is_empty_dir(wdp2.curdir)) {
			// ö�ٵ���Ϊֹ,����Ϊ��Ŀ¼,ǿ���ó���ǰ��+����
			TreeView_SetItem1(wdp->hctl, htvi, TVIF_CHILDREN, 0, 0, 0, 1, NULL);
		}
	}
	return TRUE;
}

void dir_2_tv(HWND hctl, HTREEITEM htvi, const char* rootdir, uint32_t maxdeep)
{
	tv_walk_dir_param_t		wdp;

	wdp.hctl = hctl;
	wdp.maxdeep = maxdeep;
	wdp.deep = 0;
	wdp.htvi = htvi;
	strcpy(wdp.curdir, rootdir);
	// �˴���subfolders������Ϊ1. ����Ϊ1,����ö�ٳ�����Ŀ¼/�ļ����ᱻ���ӵ�IVI_ROOT,���ڻص�������������,
	// ������������ܻ��ϵͳ����
	walk_dir_win32_deepen(rootdir, 0, cb_walk_dir_explorer, (uint32_t *)&wdp);
	
	return;
}

BOOL On_DlgDDescInitDialog(HWND hdlgP, HWND hwndFocus, LPARAM lParam)
{
	HWND hctl = GetDlgItem(hdlgP, IDC_TV_DDESC_EXPLORER);
			
	gdmgr._hdlg_ddesc = hdlgP;

	// ��ʾwelcome.bmp
	SendMessage(GetDlgItem(hdlgP, IDC_ST_WELCOME), STM_SETIMAGE, (WPARAM)IMAGE_BITMAP, (LPARAM)gdmgr._welcomebmp);
	
	// ListView_SetImageList(hwndCtl, gdmgr._himl, LVSIL_SMALL);
	TreeView_SetImageList(hctl, gdmgr._himl, TVSIL_NORMAL);

	OnLSBt(TRUE);

	return TRUE;
}

BOOL check_wok_root_folder(char *folder)
{
	char		text[_MAX_PATH];
	
	// <wok>\data\_main.cfg
	sprintf(text, "%s\\data\\_main.cfg", folder);
	return is_file(text);
}

void On_DlgDDescCommand(HWND hdlgP, int id, HWND hwndCtrl, UINT codeNotify)
{
	char		*ptr = NULL;
	int			retval;
	char		text[_MAX_PATH];
	BOOL		fok, fDir;
	
	switch (id) {
	case IDC_BT_DDESC_BROWSE:
		ptr = GetBrowseFilePath(hdlgP);
		if (!ptr) {
			break;
		}
		strcpy(text, appendbackslash(ptr));
		if (text[strlen(text) - 1] == '\\') {
			text[strlen(text) - 1] = 0;
		}
		if (!check_wok_root_folder(text)) {
			posix_print_mb("%s��������Ч��kingdom-src-x.x.xԴ����Ŀ¼��������ѡ��", appendbackslash(text));
			break;
		}
		if (strcasecmp(ptr, game_config::path.c_str())) {
			game_config::path = ptr;
			gdmgr.heros_.set_path(game_config::path.c_str());
			Edit_SetText(GetDlgItem(hdlgP, IDC_ET_DDESC_WWWROOT), appendbackslash(ptr));
			OnLSBt(TRUE);
			if (gdmgr._da != da_sync) {
				title_select(da_sync);
			} else {
				sync_enter_ui();
			}
			update_locale_dir();
		}
		break;

	case IDM_COHERENCT_ITEM0:
		// �г���Ŀ¼������ͬ���ļ�
		title_select(da_tbox);
		do_coherence(game_config::path.c_str(), basename(gdmgr._menu_text), 0);
		break;
	case IDM_COHERENCT_ITEM1:
		// �Ը�Ŀ¼������ͬ���ļ�,�����µ�ȥ�滻
		sprintf(text, "Do you want to xcopy all %s using last modified ?", basename(gdmgr._menu_text));
		retval = MessageBox(hdlgP, text, "Confirm File XCopy", MB_YESNO | MB_DEFBUTTON2); // Ĭ��: ���滻
		if (retval == IDYES) {
			title_select(da_tbox);
			do_coherence(game_config::path.c_str(), basename(gdmgr._menu_text), 1);
		}
		break;
	case IDM_COHERENCT_ITEM2:
		// �Ը�Ŀ¼������ͬ���ļ�,��ָ�����ļ�ȥ�滻
		sprintf(text, "Do you want to xcopy all %s using special file: %s ?", basename(gdmgr._menu_text), gdmgr._menu_text);
		retval = MessageBox(hdlgP, text, "Confirm File XCopy", MB_YESNO | MB_DEFBUTTON2); // Ĭ��: ���滻
		if (retval == IDYES) {
			title_select(da_tbox);
			do_coherence(game_config::path.c_str(), basename(gdmgr._menu_text), 1, gdmgr._menu_text);
		}
		break;

	case IDM_GENERATE_ITEM0:
		sprintf(text, "Do you want to extract install-material to C:\\kingdom-ins?"); 
		retval = MessageBox(hdlgP, text, "Confirm Generate", MB_YESNO | MB_DEFBUTTON2);
		if (retval == IDYES) {
			fok = extra_kingdom_ins_disk(gdmgr._menu_text, "c:\\kingdom-ins", "c:\\kingdom-ins-android\\com.freeors.kingdom");
			posix_print_mb("Extract install-material from %s to C:\\kingdom-ins, %s!!", gdmgr._menu_text, fok? "Success": "Fail");
		}
		break;

	case IDM_GENERATE_ITEM1:
		if (gdmgr._da != da_sync) {
			title_select(da_sync);
		} else {
			sync_enter_ui();
		}
		break;

	case IDM_DELETE_ITEM0:
		// ���ָ�����ļ�/Ŀ¼
		fDir = is_directory(gdmgr._menu_text);
		if (fDir) {
			sprintf(text, "Do you want to delete directory: %s?", gdmgr._menu_text); 
		} else {
			sprintf(text, "Do you want to delete file: %s?", gdmgr._menu_text); 
		}
		// Confirm Multiple File Delete 
		retval = MessageBox(hdlgP, text, "Confirm Delete", MB_YESNO | MB_DEFBUTTON2);
		if (retval == IDYES) {
			if (delfile1(gdmgr._menu_text)) {
                TreeView_DeleteItem(GetDlgItem(hdlgP, IDC_TV_DDESC_EXPLORER), (HTREEITEM)(UINT_PTR)gdmgr._menu_lparam);
				TreeView_SetChilerenByPath(GetDlgItem(hdlgP, IDC_TV_DDESC_EXPLORER), (HTREEITEM)(UINT_PTR)gdmgr._menu_lparam, dirname(gdmgr._menu_text)); 
			} else {
				posix_print_mb("Failed delete %s !", gdmgr._menu_text); 
			}
		}
		break;

	case IDM_DELETE_ITEM1:
		// ���Ŀ¼
		fDir = is_directory(gdmgr._menu_text);
		if (!fDir) {
			posix_print_mb("%s isn't directory!", gdmgr._menu_text);
			break;
		}
		sprintf(text, "Do you want to empty directory: %s?", gdmgr._menu_text); 
		retval = MessageBox(hdlgP, text, "Confirm Empty", MB_YESNO | MB_DEFBUTTON2);
		if (retval == IDYES) {
            title_select(da_tbox);
			do_empty_directory(gdmgr._menu_text);
			TreeView_SetChilerenByPath(GetDlgItem(hdlgP, IDC_TV_DDESC_EXPLORER), (HTREEITEM)(UINT_PTR)gdmgr._menu_lparam, gdmgr._menu_text);
		}
		break;

	case IDM_DELETE_ITEM2:
		// auto_*.htm
		title_select(da_tbox);
		do_xgenfile(gdmgr._menu_text, 0);
		break;

	case IDM_DELETE_ITEM3:
		// ���Ŀ¼
		sprintf(text, "Do you want to delete all x_gene_*.htm/php file in directory: %s?", gdmgr._menu_text); 
		retval = MessageBox(hdlgP, text, "Confirm Empty", MB_YESNO | MB_DEFBUTTON2);
		if (retval == IDYES) {
            title_select(da_tbox);
			do_xgenfile(gdmgr._menu_text, 1);
			TreeView_Expand(GetDlgItem(hdlgP, IDC_TV_DDESC_EXPLORER), (HTREEITEM)(UINT_PTR)gdmgr._menu_lparam, TVE_COLLAPSE);
			TreeView_SetChilerenByPath(GetDlgItem(hdlgP, IDC_TV_DDESC_EXPLORER), (HTREEITEM)(UINT_PTR)gdmgr._menu_lparam, gdmgr._menu_text);
		}
		break;
        
	default:
		break;
	}
	return;
}

void menu_delete_refresh(HMENU hmenu, char *path, BOOL fDir) 
{
	uint32_t		idx;
	uint32_t		mf_dir = fDir? MF_ENABLED: MF_GRAYED;

	// delete this
	if (strcasecmp(path, game_config::path.c_str())) {
		EnableMenuItem(hmenu, IDM_DELETE_ITEM0, MF_BYCOMMAND | MF_ENABLED);
	} else {
		EnableMenuItem(hmenu, IDM_DELETE_ITEM0, MF_BYCOMMAND | MF_GRAYED);
	}
	// directory
	for (idx = 1; idx < 10; idx ++) {
        EnableMenuItem(hmenu, IDM_DELETE_ITEM0 + idx, MF_BYCOMMAND | mf_dir);
	}

	return;
}

//
// On_DlgDDescNotify()
//
BOOL On_DlgDDescNotify(HWND hdlgP, int DlgItem, LPNMHDR lpNMHdr)
{
	LPNMTREEVIEW			lpnmitem;
	HTREEITEM				htvi;
	TVITEMEX				tvi;
	char					text[_MAX_PATH];
	POINT					point;
	
	if (gdmgr._syncing || (lpNMHdr->idFrom != IDC_TV_DDESC_EXPLORER)) {
		return FALSE;
	}

	lpnmitem = (LPNMTREEVIEW)lpNMHdr;

	// NM_RCLICK/NM_CLICK/NM_DBLCLK��Щ֪ͨ��������,�丽������û��ָ�����ĸ�Ҷ�Ӿ��,
	// ��ͨ���ж�����������ж����ĸ�Ҷ�ӱ�����?
	// 1. GetCursorPos, �õ���Ļ����ϵ�µ��������
	// 2. TreeView_HitTest1(��д��),����Ļ����ϵ�µ�������귵��ָ���Ҷ�Ӿ��
	GetCursorPos(&point);	// �õ�������Ļ����
	TreeView_HitTest1(lpNMHdr->hwndFrom, point, htvi);
	
	// NM_��ʾ��ͨ�ÿؼ���ͨ��,��Χ��(0, 99)
	// TVN_��ʾֻ��TreeViewͨ��,��Χ��(400, 499)
	if (lpNMHdr->code == NM_RCLICK) {
		//
		// �Ҽ�����: �����˵�
		//
		// ׼�������˵������ı���
		TreeView_GetItem1(lpNMHdr->hwndFrom, htvi, &tvi, TVIF_IMAGE | TVIF_PARAM | TVIF_TEXT, text);
		strcpy(gdmgr._menu_text, TreeView_FormPath(lpNMHdr->hwndFrom, htvi, dirname(game_config::path.c_str())));
		gdmgr._menu_lparam = (uint32_t)tvi.lParam;

		if (!strcasecmp(gdmgr._menu_text, game_config::path.c_str())) {
			EnableMenuItem(gdmgr._hpopup_ddesc, (UINT)(UINT_PTR)(gdmgr._hpopup_generate), MF_BYCOMMAND | MF_ENABLED);
		} else {
			EnableMenuItem(gdmgr._hpopup_ddesc, (UINT)(UINT_PTR)(gdmgr._hpopup_generate), MF_BYCOMMAND | MF_GRAYED);
		}
		EnableMenuItem(gdmgr._hpopup_ddesc, (UINT)(UINT_PTR)(gdmgr._hpopup_coherence), MF_BYCOMMAND | MF_GRAYED);
		EnableMenuItem(gdmgr._hpopup_ddesc, (UINT)(UINT_PTR)(gdmgr._hpopup_delete), MF_BYCOMMAND | MF_GRAYED);

		TrackPopupMenuEx(gdmgr._hpopup_ddesc, 0,
			point.x, 
			point.y, 
			hdlgP, 
			NULL);

		EnableMenuItem(gdmgr._hpopup_ddesc, (UINT)(UINT_PTR)(gdmgr._hpopup_generate), MF_BYCOMMAND | MF_ENABLED);
		EnableMenuItem(gdmgr._hpopup_ddesc, (UINT)(UINT_PTR)(gdmgr._hpopup_coherence), MF_BYCOMMAND | MF_ENABLED);
		
	
	} else if (lpNMHdr->code == NM_CLICK) {
		//
		// �������: ������±�������Ŀ¼,��û�����ɹ�Ҷ��,����Ҷ��
		//
		strcpy(text, TreeView_FormPath(lpNMHdr->hwndFrom, htvi, dirname(game_config::path.c_str())));
		TreeView_GetItem1(lpNMHdr->hwndFrom, htvi, &tvi, TVIF_CHILDREN, NULL);
		if (tvi.cChildren && !TreeView_GetChild(lpNMHdr->hwndFrom, htvi)) {
			dir_2_tv(lpNMHdr->hwndFrom, htvi, text, 0);
		}
		// ַַ������ʾѡ��Ŀ¼
		strcpy(text, TreeView_FormPath(lpNMHdr->hwndFrom, htvi, dirname(game_config::path.c_str())));
		if (!is_directory(text)) {
			strcpy(text, dirname(text));
		}
		// Edit_SetText(GetDlgItem(hdlgP, IDC_ET_DDESC_WWWROOT), text + strlen(dirname(game_config::path.c_str())) + 1);

	} else if (lpNMHdr->code == NM_DBLCLK) {
		//
		// Ŀ¼: չ��/�۵�Ҷ��(ϵͳ�Զ�)
		// �ļ�: ���ض�Ӧ�����
		//
		// �л����༭����
		TreeView_GetItem1(lpNMHdr->hwndFrom, htvi, &tvi, TVIF_IMAGE | TVIF_PARAM | TVIF_TEXT, text);
		if (!_stricmp(text, "hero.dat")) {
			strcpy(gdmgr._menu_text, TreeView_FormPath(lpNMHdr->hwndFrom, htvi, dirname(game_config::path.c_str())));
			gdmgr._menu_lparam = (uint32_t)tvi.lParam;
			if (gdmgr._da != da_wgen) {
				title_select(da_wgen);
			} else {
				wgen_enter_ui();
			}
		} else if (!_stricmp(text, "tb.dat") || !_stricmp(text, "tb-1.dat")) {
			strcpy(gdmgr._menu_text, TreeView_FormPath(lpNMHdr->hwndFrom, htvi, dirname(game_config::path.c_str())));
			gdmgr._menu_lparam = (uint32_t)tvi.lParam;
			if (gdmgr._da != da_tb) {
				title_select(da_tb);
			} else {
				tb_enter_ui();
			}
		} else if (!_stricmp(extname(text), "bin")) {
			strcpy(gdmgr._menu_text, TreeView_FormPath(lpNMHdr->hwndFrom, htvi, dirname(game_config::path.c_str())));
			if (wml_checksum_from_file(std::string(gdmgr._menu_text))) {
				gdmgr._menu_lparam = (uint32_t)tvi.lParam;
				if (gdmgr._da != da_cfg) {
					title_select(da_cfg);
				} else {
					cfg_enter_ui();
				}
			}
		}
		
	} else if (lpNMHdr->code == TVN_ITEMEXPANDED) {
		//
		// ��Ҷ���ѱ�չ�����۵�, ���۵�ʱɾ��������Ҷ��, �Ա��´�չ��ʱͨ��ӳ����ļ�ϵͳ
		//

		if (lpnmitem->action & TVE_COLLAPSE) {
			TreeView_Walk(lpNMHdr->hwndFrom, lpnmitem->itemNew.hItem, FALSE, NULL, NULL, TRUE);
			TreeView_SetChilerenByPath(lpNMHdr->hwndFrom, htvi, TreeView_FormPath(lpNMHdr->hwndFrom, htvi, dirname(game_config::path.c_str())));
		}
	}

    return FALSE;
}

void On_DlgDDescDrawItem(HWND hdlgP, const DRAWITEMSTRUCT *lpdis)
{
	HDC					hdcMem; 
    hdcMem = CreateCompatibleDC(lpdis->hDC); 

	if (lpdis->itemState & ODS_SELECTED) { // if selected 
		SelectObject(hdcMem, gdmgr._hbm_refresh_select); 
	} else {
		SelectObject(hdcMem, gdmgr._hbm_refresh_unselect); 
	}

    // Destination 
    StretchBlt( 
                lpdis->hDC,         // destination DC 
                lpdis->rcItem.left, // x upper left 
                lpdis->rcItem.top,  // y upper left 
 
                // The next two lines specify the width and 
                // height. 
                lpdis->rcItem.right - lpdis->rcItem.left, 
                lpdis->rcItem.bottom - lpdis->rcItem.top, 
                hdcMem,    // source device context 
                0, 0,      // x and y upper left 
                24,        // source bitmap width 
                24,        // source bitmap height 
                SRCCOPY);  // raster operation 

	DeleteDC(hdcMem); 

	return;
}

void On_DlgDDescDestroy(HWND hdlgP)
{
	return;
}

BOOL CALLBACK DlgDDescProc(HWND hdlgP, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch(uMsg) {
	case WM_INITDIALOG:
		return On_DlgDDescInitDialog(hdlgP, (HWND)wParam, lParam);		
	HANDLE_MSG(hdlgP, WM_COMMAND, On_DlgDDescCommand);
	HANDLE_MSG(hdlgP, WM_NOTIFY,  On_DlgDDescNotify);
	HANDLE_MSG(hdlgP, WM_DESTROY, On_DlgDDescDestroy);
	// HANDLE_MSG(hdlgP, WM_DRAWITEM, On_DlgDDescDrawItem);
	}
	return FALSE;
}

// 
// ���ܺ���
//

// Ϊ�˱Ƚ��Ϸ���,�˺���Ҫ��iSubItem=0��������iImage,��Ŀ¼������0,�ļ��Ļ����Ǳ�0���ֵ
int CALLBACK fn_dvr_compare(LPARAM lParam1, LPARAM lParam2, LPARAM lParamSort)
{
	HWND				hctl = (HWND)lParamSort;
	LVITEM				lvi1, lvi2;
	int					lvi1_type, lvi2_type;
	char				name1[_MAX_PATH], name2[_MAX_PATH];

	lvi1.iItem = (int)lParam1;
	lvi1.mask = LVIF_TEXT | LVIF_IMAGE;
    lvi1.iSubItem = 0;
	lvi1.pszText = name1;
    lvi1.cchTextMax = _MAX_PATH;
    ListView_GetItem(hctl, &lvi1);

	lvi2.iItem = (int)lParam2;
	lvi2.mask = LVIF_TEXT | LVIF_IMAGE;
    lvi2.iSubItem = 0;
	lvi2.pszText = name2;
    lvi2.cchTextMax = _MAX_PATH;
    ListView_GetItem(hctl, &lvi2);

	lvi1_type = lvi1.iImage? 1: 0;
	lvi2_type = lvi2.iImage? 1: 0;
	if (lvi1_type < lvi2_type) {
		// lvi1��Ŀ¼, lvi2���ļ�
		return -1;
	} else if (lvi1_type > lvi2_type) {
		// lvi1���ļ�, lvi2��Ŀ¼
		return 1;
	} else {
		// lvi1��lvi2����Ŀ¼���ļ�,ֻ�����ַ����Ƚ�
		return strcasecmp(name1, name2);
	}
}

int select_iimage_according_fname(char *name, uint32_t flags)
{
	int		iImage;
	char	*ext;

	if (flags & FILE_ATTRIBUTE_DIRECTORY) {
		iImage = gdmgr._iico_dir;
	} else {
		ext = extname(name);
		if (!strcasecmp(ext, "csf")) {
			iImage = gdmgr._iico_csf;
		} else if (!strcasecmp(ext, "txt")) {
			iImage = gdmgr._iico_txt;
		} else if (!strcasecmp(ext, "nma")) {
			iImage = gdmgr._iico_nma;
		} else if (!strcasecmp(ext, "ini")) {
			iImage = gdmgr._iico_ini;
		} else {
			iImage = gdmgr._iico_unknown;
		}
	}
	return iImage;
}

void OnLSBt(BOOL checkrunning)
{
	ULARGE_INTEGER ullFreeBytesAvailable, ullTotalNumberOfBytes;
	char text[_MAX_PATH];
	HRESULT hr = S_OK;
	HWND hctl = GetDlgItem(gdmgr._hdlg_ddesc, IDC_TV_DDESC_EXPLORER);

	// 1. ɾ��Treeview��������
	TreeView_DeleteAllItems(hctl);

	// 2. ��ַ���༭��
	Edit_SetText(GetDlgItem(gdmgr._hdlg_ddesc, IDC_ET_DDESC_WWWROOT), appendbackslash(game_config::path.c_str()));

	// 3. ��TreeView����һ������
	gdmgr._htvroot = TreeView_AddLeaf(hctl, TVI_ROOT);
	strcpy(text, basename(game_config::path.c_str()));
	// ����һ��Ҫ��TVIF_CHILDREN, ��������۵����жϳ���cChildrenΪ0, �ٲ���չ��
	TreeView_SetItem1(hctl, gdmgr._htvroot, TVIF_TEXT | TVIF_PARAM | TVIF_CHILDREN, (LPARAM)gdmgr._htvroot, 0, 0, 
		is_empty_dir(game_config::path.c_str())? 0: 1, text);
	dir_2_tv(hctl, gdmgr._htvroot, game_config::path.c_str(), 0);

	// 4. ����״̬
	strncpy(text, game_config::path.c_str(), 2);
	text[2] = '\\';
	text[3] = 0;
	GetDiskFreeSpaceEx(text, &ullFreeBytesAvailable, &ullTotalNumberOfBytes, NULL);
	strcpy(text, format_space_u64n(ullTotalNumberOfBytes.QuadPart));
	Edit_SetText(GetDlgItem(gdmgr._hdlg_ddesc, IDC_ET_DDESC_SUBAREA), formatstr("%s (Avail Space %s)", text, format_space_u64n(ullFreeBytesAvailable.QuadPart)));

	return;
}

void OnDeleteBt(HWND hdlgP, char *fname)
{
	HWND			hwndCtl = GetDlgItem(hdlgP, IDC_LV_DDESC_EXPLORER);

	// �Զ�ˢ��
	OnLSBt(TRUE);
	return;
}

BOOL copy_root_files(char* src, char* dst)
{
	char				szCurrDir[_MAX_PATH], text1[_MAX_PATH], text2[_MAX_PATH];
	HANDLE				hFind;
	WIN32_FIND_DATA		finddata;
	BOOL				fok, fret = TRUE;
	
	if (!src || !src[0] || !dst || !dst[0]) {
		return FALSE;
	}
		
	GetCurrentDirectory(_MAX_PATH, szCurrDir);
	SetCurrentDirectory(appendbackslash(src));
	hFind = FindFirstFile("*.*", &finddata);
	fok = (hFind != INVALID_HANDLE_VALUE);

	while (fok) {
		if (finddata.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
			// Ŀ¼
		} else {
			// �ļ�
			sprintf(text1, "%s\\%s", src, finddata.cFileName);
			sprintf(text2, "%s\\%s", dst, finddata.cFileName);
			fret = CopyFile(text1, text2, FALSE);
			if (!fret) {
				posix_print_mb("copy file from %s to %s fail", text1, text2);
				break;
			}
		}
		fok = FindNextFile(hFind, &finddata);
	}
	if (hFind != INVALID_HANDLE_VALUE) {
		FindClose(hFind);
	}

	SetCurrentDirectory(szCurrDir);
	return fret;
}

typedef struct {
	std::string src_campaign_dir;
	std::string ins_campaign_dir;
} walk_campaign_param_t;

static BOOL cb_walk_campaign(char* name, uint32_t flags, uint64_t len, int64_t lastWriteTime, uint32_t* ctx)
{
	char text1[_MAX_PATH], text2[_MAX_PATH];
	BOOL fok;
	walk_campaign_param_t* wcp = (walk_campaign_param_t*)ctx;

	if (flags & FILE_ATTRIBUTE_DIRECTORY ) {
		sprintf(text2, "%s\\%s", wcp->ins_campaign_dir.c_str(), name);
		MakeDirectory(std::string(text2)); // !!��Ҫ��<data>/campaigns/{campaign}�½�imagesĿ¼
		sprintf(text1, "%s\\%s\\images", wcp->src_campaign_dir.c_str(), name);
		sprintf(text2, "%s\\%s\\images", wcp->ins_campaign_dir.c_str(), name);
		if (is_directory(text1)) {
			posix_print("<data>, copy %s to %s ......\n", text1, text2);
			fok = copyfile(text1, text2);
			if (!fok) {
				posix_print_mb("����Ŀ¼����%s��%s��ʧ��", text1, text2);
				return FALSE;
			}
		}
	}
	return TRUE;
}

BOOL extra_kingdom_ins_disk(char* kingdom_src, char* kingdom_ins, char* kingdom_ins_android)
{
	char text1[_MAX_PATH], text2[_MAX_PATH];
	BOOL fok;
	walk_campaign_param_t wcp;

	MakeDirectory(std::string(kingdom_ins));

	// ���Ŀ¼
	fok = delfile1(kingdom_ins);
	if (!fok) {
		posix_print_mb("ɾ��Ŀ¼: %s��ʧ��", kingdom_ins);
		return fok;
	}
	fok = delfile1(kingdom_ins_android);
	if (!fok) {
		posix_print_mb("ɾ��Ŀ¼: %s��ʧ��", kingdom_ins_android);
		// return fok;
	}

	//
	// <kingdom-src>\data
	//

	// 1. images in all campaigns
	wcp.src_campaign_dir = std::string(kingdom_src) + "\\data\\campaigns";
	wcp.ins_campaign_dir = std::string(kingdom_ins) + "\\data\\campaigns";
	fok = walk_dir_win32_deepen(wcp.src_campaign_dir.c_str(), 0, cb_walk_campaign, (uint32_t *)&wcp);
	if (!fok) {
		return fok;
	}
	wcp.ins_campaign_dir = std::string(kingdom_ins_android) + "\\data\\campaigns";
	fok = walk_dir_win32_deepen(wcp.src_campaign_dir.c_str(), 0, cb_walk_campaign, (uint32_t *)&wcp);
	if (!fok) {
		return fok;
	}

	// 2. images in <data>\core root
	MakeDirectory(std::string("") + kingdom_ins + "\\data\\core");
	sprintf(text1, "%s\\data\\core\\images", kingdom_src);
	sprintf(text2, "%s\\data\\core\\images", kingdom_ins);
	posix_print("<data>, copy %s to %s ......\n", text1, text2);
	fok = copyfile(text1, text2);
	if (!fok) {
		posix_print_mb("����Ŀ¼����%s��%s��ʧ��", text1, text2);
		goto exit;
	}
	// android
	MakeDirectory(std::string("") + kingdom_ins_android + "\\data\\core");
	sprintf(text2, "%s\\data\\core\\images", kingdom_ins_android);
	posix_print("<data>, copy %s to %s ......\n", text1, text2);
	fok = copyfile(text1, text2);
	if (!fok) {
		posix_print_mb("����Ŀ¼����%s��%s��ʧ��", text1, text2);
		goto exit;
	}
	// 3. music in <data>\core root
	sprintf(text1, "%s\\data\\core\\music", kingdom_src);
	sprintf(text2, "%s\\data\\core\\music", kingdom_ins);
	posix_print("<data>, copy %s to %s ......\n", text1, text2);
	fok = copyfile(text1, text2);
	if (!fok) {
		posix_print_mb("����Ŀ¼����%s��%s��ʧ��", text1, text2);
		goto exit;
	}
	// android
	sprintf(text2, "%s\\data\\core\\music", kingdom_ins_android);
	posix_print("<data>, copy %s to %s ......\n", text1, text2);
	fok = copyfile(text1, text2);
	if (!fok) {
		posix_print_mb("����Ŀ¼����%s��%s��ʧ��", text1, text2);
		goto exit;
	}
	// 4. sounds in <data>\core root
	sprintf(text1, "%s\\data\\core\\sounds", kingdom_src);
	sprintf(text2, "%s\\data\\core\\sounds", kingdom_ins);
	posix_print("<data>, copy %s to %s ......\n", text1, text2);
	fok = copyfile(text1, text2);
	if (!fok) {
		posix_print_mb("����Ŀ¼����%s��%s��ʧ��", text1, text2);
		goto exit;
	}
	// android
	sprintf(text2, "%s\\data\\core\\sounds", kingdom_ins_android);
	posix_print("<data>, copy %s to %s ......\n", text1, text2);
	fok = copyfile(text1, text2);
	if (!fok) {
		posix_print_mb("����Ŀ¼����%s��%s��ʧ��", text1, text2);
		goto exit;
	}

	sprintf(text1, "%s\\data\\hardwired", kingdom_src);
	sprintf(text2, "%s\\data\\hardwired", kingdom_ins);
	posix_print("<data>, copy %s to %s ......\n", text1, text2);
	fok = copyfile(text1, text2);
	if (!fok) {
		posix_print_mb("����Ŀ¼����%s��%s��ʧ��", text1, text2);
		goto exit;
	}
	// android
	sprintf(text2, "%s\\data\\hardwired", kingdom_ins_android);
	posix_print("<data>, copy %s to %s ......\n", text1, text2);
	fok = copyfile(text1, text2);
	if (!fok) {
		posix_print_mb("����Ŀ¼����%s��%s��ʧ��", text1, text2);
		goto exit;
	}

	sprintf(text1, "%s\\data\\lua", kingdom_src);
	sprintf(text2, "%s\\data\\lua", kingdom_ins);
	posix_print("<data>, copy %s to %s ......\n", text1, text2);
	fok = copyfile(text1, text2);
	if (!fok) {
		posix_print_mb("����Ŀ¼����%s��%s��ʧ��", text1, text2);
		goto exit;
	}
	// android
	sprintf(text2, "%s\\data\\lua", kingdom_ins_android);
	posix_print("<data>, copy %s to %s ......\n", text1, text2);
	fok = copyfile(text1, text2);
	if (!fok) {
		posix_print_mb("����Ŀ¼����%s��%s��ʧ��", text1, text2);
		goto exit;
	}

	sprintf(text1, "%s\\data\\tools", kingdom_src);
	sprintf(text2, "%s\\data\\tools", kingdom_ins);
	posix_print("<data>, copy %s to %s ......\n", text1, text2);
	fok = copyfile(text1, text2);
	if (!fok) {
		posix_print_mb("����Ŀ¼����%s��%s��ʧ��", text1, text2);
		goto exit;
	}
	// android
	sprintf(text2, "%s\\data\\tools", kingdom_ins_android);
	posix_print("<data>, copy %s to %s ......\n", text1, text2);
	fok = copyfile(text1, text2);
	if (!fok) {
		posix_print_mb("����Ŀ¼����%s��%s��ʧ��", text1, text2);
		goto exit;
	}
	// <kingdom-src>\data\_main_cfg
	sprintf(text1, "%s\\data\\_main.cfg", kingdom_src);
	sprintf(text2, "%s\\data\\_main.cfg", kingdom_ins);
	posix_print("<data>, copy %s to %s ......\n", text1, text2);
	fok = copyfile(text1, text2);
	if (!fok) {
		posix_print_mb("�����ļ�����%s��%s��ʧ��", text1, text2);
		goto exit;
	}
	// android
	sprintf(text2, "%s\\data\\_main.cfg", kingdom_ins_android);
	posix_print("<data>, copy %s to %s ......\n", text1, text2);
	fok = copyfile(text1, text2);
	if (!fok) {
		posix_print_mb("�����ļ�����%s��%s��ʧ��", text1, text2);
		goto exit;
	}
	//
	// <kingdom-src>\fonts
	//
	sprintf(text1, "%s\\fonts", kingdom_src);
	sprintf(text2, "%s\\fonts", kingdom_ins);
	posix_print("<data>, copy %s to %s ......\n", text1, text2);
	fok = copyfile(text1, text2);
	if (!fok) {
		posix_print_mb("����Ŀ¼����%s��%s��ʧ��", text1, text2);
		goto exit;
	}
	// android
	sprintf(text2, "%s\\fonts", kingdom_ins_android);
	posix_print("<data>, copy %s to %s ......\n", text1, text2);
	fok = copyfile(text1, text2);
	if (!fok) {
		posix_print_mb("����Ŀ¼����%s��%s��ʧ��", text1, text2);
		goto exit;
	}
	//
	// <kingdom-src>\images
	//
	sprintf(text1, "%s\\images", kingdom_src);
	sprintf(text2, "%s\\images", kingdom_ins);
	posix_print("<data>, copy %s to %s ......\n", text1, text2);
	fok = copyfile(text1, text2);
	if (!fok) {
		posix_print_mb("����Ŀ¼����%s��%s��ʧ��", text1, text2);
		goto exit;
	}
	// android
	sprintf(text2, "%s\\images", kingdom_ins_android);
	posix_print("<data>, copy %s to %s ......\n", text1, text2);
	fok = copyfile(text1, text2);
	if (!fok) {
		posix_print_mb("����Ŀ¼����%s��%s��ʧ��", text1, text2);
		goto exit;
	}
	//
	// <kingdom-src>\manual
	//
	sprintf(text1, "%s\\manual", kingdom_src);
	sprintf(text2, "%s\\manual", kingdom_ins);
	posix_print("<data>, copy %s to %s ......\n", text1, text2);
	fok = copyfile(text1, text2);
	if (!fok) {
		posix_print_mb("����Ŀ¼����%s��%s��ʧ��", text1, text2);
		goto exit;
	}
	// andorid don't copy manual

	//
	// <kingdom-src>\sounds
	//
	sprintf(text1, "%s\\sounds", kingdom_src);
	sprintf(text2, "%s\\sounds", kingdom_ins);
	posix_print("<data>, copy %s to %s ......\n", text1, text2);
	fok = copyfile(text1, text2);
	if (!fok) {
		posix_print_mb("����Ŀ¼����%s��%s��ʧ��", text1, text2);
		goto exit;
	}
	// android
	sprintf(text2, "%s\\sounds", kingdom_ins_android);
	posix_print("<data>, copy %s to %s ......\n", text1, text2);
	fok = copyfile(text1, text2);
	if (!fok) {
		posix_print_mb("����Ŀ¼����%s��%s��ʧ��", text1, text2);
		goto exit;
	}
	//
	// <kingdom-src>\translations
	//
	sprintf(text1, "%s\\translations", kingdom_src);
	sprintf(text2, "%s\\translations", kingdom_ins);
	posix_print("<data>, copy %s to %s ......\n", text1, text2);
	fok = copyfile(text1, text2);
	if (!fok) {
		posix_print_mb("����Ŀ¼����%s��%s��ʧ��", text1, text2);
		goto exit;
	}
	// android
	sprintf(text2, "%s\\translations", kingdom_ins_android);
	posix_print("<data>, copy %s to %s ......\n", text1, text2);
	fok = copyfile(text1, text2);
	if (!fok) {
		posix_print_mb("����Ŀ¼����%s��%s��ʧ��", text1, text2);
		goto exit;
	}
	//
	// <kingdom-src>\xwml
	//
	sprintf(text1, "%s\\xwml", kingdom_src);
	sprintf(text2, "%s\\xwml", kingdom_ins);
	posix_print("<data>, copy %s to %s ......\n", text1, text2);
	fok = copyfile(text1, text2);
	if (!fok) {
		posix_print_mb("����Ŀ¼����%s��%s��ʧ��", text1, text2);
		goto exit;
	}
	// android
	sprintf(text2, "%s\\xwml", kingdom_ins_android);
	posix_print("<data>, copy %s to %s ......\n", text1, text2);
	fok = copyfile(text1, text2);
	if (!fok) {
		posix_print_mb("����Ŀ¼����%s��%s��ʧ��", text1, text2);
		goto exit;
	}

	fok = copy_root_files(kingdom_src, kingdom_ins);
exit:
	if (!fok) {
		delfile1(kingdom_ins);
	}

	return fok;
}