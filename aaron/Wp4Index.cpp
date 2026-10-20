// WP-4 index: where each previously "unused" Aaron.dll ordinal lives in the port.
//
// tools/engine_coverage.py decides "implemented" by looking for the original address in the BODY of a .cpp (or on a header line that
// contains the inline body). Many of the ordinals below are inline members or are provided by C++ itself (class ids, per-class thunks,
// destructors); this file lists them, one line per address, with the port symbol that implements them. It contains no code.
// Format:  0xADDRESS  ord N[,M..]  Name   -> file:line of the declaration / definition   [note]
// The per-class operator new / delete thunks are NOT listed: they are class-level C++ artefacts mapped to the global operators
// (see RObject.h) and stay "n.a." in the coverage report.

// 0x10001022  ord 2  RObject::RObject  -> aaron/RObject.h:24
// 0x10001043  ord 137  RObject::AddRef  -> aaron/RObject.h:29
// 0x1000105b  ord 562  RObject::Release  -> aaron/RObject.h:31
// 0x100010c0  ord 366  RObject::GetObjClassId  -> aaron/RObject.h:35   [returns 0x20000]
// 0x10001100  ord 4  MemBuffer::MemBuffer  -> aaron/Containers.cpp:17
// 0x100011ff  ord 60  MemBuffer::~MemBuffer  -> aaron/Containers.cpp:31
// 0x10001260  ord 145  MemBuffer::Append  -> aaron/Containers.cpp:75
// 0x100012be  ord 147  MemBuffer::AppendString  -> aaron/Containers.cpp:82
// 0x100012e9  ord 146  MemBuffer::AppendLine  -> aaron/Containers.cpp:86
// 0x1000134b  ord 161  MemBuffer::Decommit  -> aaron/Containers.cpp:121
// 0x100013ef  ord 180  MemBuffer::Reset  -> aaron/Containers.cpp:130
// 0x10001444  ord 201  MemBuffer::Free  -> aaron/Containers.cpp:36
// 0x100014e6  ord 303  MemBuffer::Stats  -> aaron/Containers.cpp:145
// 0x10001511  ord 351  MemBuffer::Info  -> aaron/Containers.cpp:138
// 0x10001557  ord 403  MemBuffer::Insert  -> aaron/Containers.cpp:93
// 0x100015f9  ord 405  MemBuffer::InsertString  -> aaron/Containers.cpp:102
// 0x10001628  ord 437  MemBuffer::Lock  -> aaron/Containers.h:51
// 0x10001643  ord 567  MemBuffer::Remove  -> aaron/Containers.cpp:106
// 0x100016ea  ord 571  MemBuffer::RemoveString  -> aaron/Containers.cpp:116
// 0x10001727  ord 661  MemBuffer::SetSize  -> aaron/Containers.cpp:47
// 0x10001943  ord 709  MemBuffer::Unlock  -> aaron/Containers.h:53
// 0x10001990  ord 59  RObject::~RObject  -> aaron/RObject.h:28   [base-class destructor]
// 0x10001b74  ord 40  LinkItem::LinkItem  -> aaron/Containers.cpp:150
// 0x10001be1  ord 73  LinkItem::~LinkItem  -> aaron/Containers.cpp:152
// 0x10001c33  ord 443  LinkItem::Reposition  -> aaron/Containers.cpp:196
// 0x10001c64  ord 39  LinkList::LinkList  -> aaron/Containers.h:103
// 0x10001c96  ord 72  LinkList::~LinkList  -> aaron/Containers.cpp:205
// 0x10001ce8  ord 155  LinkList::DeleteAll  -> aaron/Containers.cpp:207
// 0x10001d2d  ord 189  LinkList::ForEach  -> aaron/Containers.cpp:211   [original releases the NEXT item (UAF bug); port does not]
// 0x10001d88  ord 419  LinkList::Contains  -> aaron/Containers.cpp:222
// 0x10001e30  ord 381  LinkList::GetObjClassId  -> aaron/Containers.h:105   [0x20002]
// 0x10001e40  ord 382  LinkItem::GetObjClassId  -> aaron/Containers.h:85   [0x20001]
// 0x10001f78  ord 45  PtrArray::PtrArray_Copy  -> aaron/RObject.cpp:33
// 0x10002150  ord 162  PtrArray::Shrink  -> aaron/RObject.cpp:106
// 0x100021c5  ord 190  PtrArray::ForEach  -> aaron/RObject.cpp:116
// 0x10002277  ord 442  PtrArray::MoveElement  -> aaron/RObject.cpp:122
// 0x100024fb  ord 176  PtrArray::DeleteAt  -> aaron/RObject.cpp:130
// 0x10002610  ord 157  PtrArray::Reset  -> aaron/RObject.h:80
// 0x1000286b  ord 57  TreeNode::TreeNode_Root  -> aaron/RObject.cpp:146
// 0x100028d6  ord 56  TreeNode::TreeNode_Parent  -> aaron/RObject.cpp:147
// 0x100029a7  ord 139  TreeNode::AdoptRun  -> aaron/RObject.cpp:204
// 0x10002a03  ord 140  TreeNode::AdoptChildrenOf  -> aaron/RObject.cpp:218
// 0x10002a86  ord 191  TreeNode::ForEachChild  -> aaron/RObject.cpp:235   [original releases the NEXT child (bug); port does not]
// 0x10002ae1  ord 404  TreeNode::WedgeUnder  -> aaron/RObject.cpp:222
// 0x10002b22  ord 412  TreeNode::HasAncestor  -> aaron/RObject.cpp:229   [original crashes (null deref) when the ancestor is not found]
// 0x10002b58  ord 655  TreeNode::Reparent  -> aaron/RObject.cpp:189
// 0x10002b95  ord 698  TreeNode::ForEachFromHere  -> aaron/RObject.cpp:249
// 0x10002c1c  ord 55  TreeRoot::TreeRoot  -> aaron/RObject.h:123
// 0x10002c53  ord 82  TreeRoot::~TreeRoot  -> aaron/RObject.cpp:263
// 0x10002cd5  ord 660  TreeRoot::SetRoot  -> aaron/RObject.cpp:271
// 0x10002d70  ord 389  TreeRoot::GetObjClassId  -> aaron/RObject.h:125   [0x20004]
// 0x10002d80  ord 390  TreeNode::GetBaseClassId  -> aaron/RObject.h:149   [0x20003]
// 0x10002fab  ord 224  Button::Nop224  -> aaron/Sprites.h:221   [empty slot-3 virtual of the Button-like vtable 0x1001d4e0]
// 0x100032be  ord 223  Button::Nop223  -> aaron/Sprites.h:221   [empty slot-3 virtual of the Button vtable]
// 0x10003510  ord 577  Widget::NopVirtual2  -> aaron/Scene.h:63   [shared empty virtual (Scene::OnReady/OnClose, Widget slot 18, ...)]
// 0x10003520  ord 453,456,471,473  Widget::NopVirtual  -> aaron/Input.h:52   [shared empty virtual (slot 2/7/19, ControlWin/Button/..., InputSource::OnBounds)]
// 0x10003540  ord 461,462,555  Scene::OnCommand  -> aaron/Application.h:62   [shared "return 0" thunk (ords 461/462/555: Scene::OnCommand, Application::OnCommand)]
// 0x10003570  ord 368  Button::GetClassId  -> aaron/Sprites.h:220   [0x20107]
// 0x10003580  ord 259  Button::GetDrawObj  -> aaron/Sprites.h:225   [CDrawObj at +0x44: C++ inheritance (thunk)]
// 0x10003c7b  ord 625  Dialog::SetDefaultButton  -> aaron/Sprites.h:267
// 0x10003d00  ord 369  Dialog::GetClassId  -> aaron/Sprites.h:263   [0x20109]
// 0x10003d10  ord 257  Dialog::GetPickFlags  -> aaron/Sprites.h:264   [returns 0x80]
// 0x10003d70  ord 48  Node::Node_TimerRoot  -> aaron/Widget.cpp:322   [Node(TreeRoot*) + TimerMgr subscription; the port's Scene does not need the subscription]
// 0x10003e2d  ord 238  Node::Nop238  -> aaron/Widget.h:162
// 0x10003e72  ord 33  Node::Node_TreeRoot  -> aaron/Widget.cpp:322
// 0x1000484d  ord 239  Node::Nop239  -> aaron/Widget.h:163
// 0x10004a0d  ord 506  Widget::ToggleCaptured  -> aaron/Widget.cpp:41
// 0x10004c10  ord 380  Node::GetClassId  -> aaron/Types.h:58   [0x20100]
// 0x10004c20  ord 384  ResNode::GetClassId  -> aaron/Types.h:59   [0x20101: CID_CONTAINER1 (the Widget base part; no separate class in the port)]
// 0x10004c30  ord 391  Container::GetClassId  -> aaron/Types.h:60   [0x20102: CID_CONTAINER2 (enum only; never instantiated by any game)]
// 0x10004f71  ord 353  AnimSprite::GetTiming  -> aaron/Sprites.h:89
// 0x10004f94  ord 664  AnimSprite::SetTiming  -> aaron/Sprites.h:89
// 0x10005055  ord 25  AnimPolySprite::AnimPolySprite_Line  -> aaron/SpriteWidgets.cpp:178
// 0x10005167  ord 26  AnimPolySprite::AnimPolySprite_Points  -> aaron/SpriteWidgets.cpp:178
// 0x1000527d  ord 231  AnimPolySprite::Nop231  -> aaron/Sprites.h:154
// 0x100053be  ord 232  AnimRectSprite::Nop232  -> aaron/Sprites.h:185
// 0x10005575  ord 233  AnimTextSprite::Nop233  -> aaron/Sprites.h:205
// 0x10005702  ord 143  DibSprite::AnimateList  -> aaron/SpriteWidgets.cpp:28   [the +0x40 twin of ord 144 (WP-2 had attributed 144 to DibSprite)]
// 0x100057c0  ord 18  PolySprite::PolySprite_Line  -> aaron/SpriteWidgets.cpp:162
// 0x100058b9  ord 19  PolySprite::PolySprite_Points  -> aaron/SpriteWidgets.cpp:162
// 0x100059b6  ord 227  PolySprite::Nop227  -> aaron/Sprites.h:137
// 0x10005b0d  ord 21  TextSprite::TextSprite  -> aaron/SpriteWidgets.cpp:197
// 0x10005c71  ord 229  TextSprite::Nop229  -> aaron/Sprites.h:168
// 0x100067f0  ord 265  AnimSprite::GetDrawObj  -> aaron/Sprites.h:22   [CDrawObj at +0xdc (inheritance thunk)]
// 0x10006820  ord 482  AnimSprite::Create  -> aaron/Sprites.h:24   [Widget::Create + CDrawObj::Init (+0xdc)]
// 0x10006860  ord 511  AnimSprite::OnHideChange  -> aaron/Sprites.h:25   [CAnimObj::Pause(hidden)]
// 0x10006880  ord 550  AnimSprite::HitTest  -> aaron/Sprites.h:29   [IsPicked (+0xdc)]
// 0x100068b0  ord 679,680,681,682  AnimSprite::Show_Thunk  -> aaron/SpriteWidgets.cpp:191   [-> AnimSprite::Show (ord 142); ords 679-682 (AnimSprite and the Anim Poly/Rect/Text sprites)]
// 0x100068d0  ord 211,212,213,214,260,261,262,263,264  DibSprite::GetDrawObj  -> aaron/Sprites.h:21   [thunk +0x40 (ords 211-214 GetAnimObj of the Anim sprites, 260-264 GetDrawObj of Dib/Poly/Rect/Text sprites)]
// 0x10006900  ord 266,267,268  AnimPolySprite::GetDrawObj  -> aaron/Sprites.h:22   [thunk +0xdc (ords 266-268: AnimPoly/AnimRect/AnimText sprites)]
// 0x10006930  ord 512,513,514  AnimPolySprite::OnHideChange  -> aaron/Sprites.h:25   [CAnimObj::Pause (ords 512-514)]
// 0x10006950  ord 551,552,553  AnimPolySprite::HitTest  -> aaron/SpriteWidgets.cpp:195   [IsPicked +0xdc (ords 551-553)]
// 0x10006980  ord 483,484,485  AnimPolySprite::Create  -> aaron/SpriteWidgets.cpp:190   [(ords 483-485)]
// 0x100069c0  ord 370,371,373,374,375,377  DibSprite::GetClassId  -> aaron/Sprites.h:30   [0x20103 for 6 classes]
// 0x100069d0  ord 547,548,549  PolySprite::HitTest  -> aaron/SpriteWidgets.cpp:176   [IsPicked +0x40 (ords 547-549)]
// 0x10006a00  ord 372,376  RectSprite::GetClassId  -> aaron/Sprites.h:31   [0x20106 for the GDI sprites (ords 372, 376)]
// 0x10006a10  ord 478,479,480,481  PolySprite::Create  -> aaron/SpriteWidgets.cpp:174   [(ords 478-481)]
// 0x10006a50  ord 675,676,677,678  PolySprite::Show  -> aaron/SpriteWidgets.cpp:175   [= Widget::Show (ords 675-678)]
// 0x10006d83  ord 525  Timer::PostNotify  -> aaron/TimerWidget.cpp:32
// 0x10006f40  ord 378  Timer::GetClassId  -> aaron/Sprites.h:306   [0x20105]
// 0x10006fb2  ord 30  ControlWin::ControlWin  -> aaron/ControlWin.cpp:12   [model of a native child control (no SDL equivalent)]
// 0x1000715a  ord 69  ControlWin::~ControlWin  -> aaron/ControlWin.cpp:37
// 0x100071be  ord 219  ControlWin::GetClientRectScene  -> aaron/ControlWin.cpp:57
// 0x100071f5  ord 235  ControlWin::Nop235  -> aaron/ControlWin.h:33
// 0x10007202  ord 284  ControlWin::GetClientWidth  -> aaron/ControlWin.cpp:79
// 0x1000721e  ord 347  ControlWin::GetClientSize  -> aaron/ControlWin.cpp:74
// 0x1000724f  ord 358  ControlWin::GetText  -> aaron/ControlWin.cpp:107
// 0x10007271  ord 394  ControlWin::GetClientHeight  -> aaron/ControlWin.cpp:80
// 0x10007627  ord 444  ControlWin::SetPos  -> aaron/ControlWin.cpp:87
// 0x1000770d  ord 467  ControlWin::Capture  -> aaron/ControlWin.h:37   [EnableWindow (inverted by EnableCount - original quirk)]
// 0x1000772b  ord 486  ControlWin::Create  -> aaron/ControlWin.cpp:41
// 0x10007778  ord 540  ControlWin::PostMessage  -> aaron/ControlWin.h:70   [delivered at once]
// 0x1000779e  ord 554  ControlWin::HitTest  -> aaron/ControlWin.cpp:82
// 0x100077d7  ord 591  ControlWin::SendMessage  -> aaron/ControlWin.cpp:140
// 0x100077fd  ord 637  ControlWin::SetFont  -> aaron/ControlWin.cpp:114
// 0x1000786b  ord 666  ControlWin::SetText  -> aaron/ControlWin.cpp:103
// 0x1000788e  ord 668  ControlWin::SetTextColor  -> aaron/ControlWin.cpp:120   [Ghidra: Concurrency::details::RealizedChore::Initialize (false library match)]
// 0x100078b7  ord 605  ControlWin::SetBackColor  -> aaron/ControlWin.cpp:126
// 0x100078d7  ord 683  ControlWin::Show  -> aaron/ControlWin.cpp:47
// 0x10007922  ord 690  ControlWin::Resize  -> aaron/ControlWin.cpp:98
// 0x1000794c  ord 31  EditControl::EditControl  -> aaron/ControlWin.cpp:236   [class EDIT]
// 0x1000799e  ord 669  EditControl::SetLimit  -> aaron/ControlWin.h:107   [EM_LIMITTEXT]
// 0x100079be  ord 589  EditControl::SetSel  -> aaron/ControlWin.h:107   [EM_SETSEL]
// 0x10007a60  ord 379  ControlWin::GetClassId  -> aaron/ControlWin.h:32   [0x20104]
// 0x10007a70  ord 396  ControlWin::GetHWnd  -> aaron/ControlWin.h:34
// 0x10007d4f  ord 188  AccelTable::ForEach  -> aaron/Managers.cpp:134
// 0x10008350  ord 6  Cursor::Cursor  -> aaron/Cursors.cpp:8
// 0x100083d2  ord 63  Cursor::~Cursor  -> aaron/Cursors.cpp:12
// 0x10008441  ord 10  ResCursor::ResCursor  -> aaron/Cursors.cpp:16
// 0x100084bc  ord 8  DibCursor::DibCursor  -> aaron/Cursors.cpp:20
// 0x1000859b  ord 65  DibCursor::~DibCursor  -> aaron/Cursors.cpp:31
// 0x1000862d  ord 597  DibCursor::Handle  -> aaron/Cursors.cpp:41
// 0x1000865e  ord 133  DibCursor::Activate  -> aaron/Cursors.cpp:35
// 0x100086c4  ord 7  AnimCursor::AnimCursor  -> aaron/Cursors.cpp:47
// 0x100087e0  ord 64  AnimCursor::~AnimCursor  -> aaron/Cursors.cpp:58
// 0x100088ae  ord 596  AnimCursor::Handle  -> aaron/Cursors.cpp:72
// 0x100088e8  ord 132  AnimCursor::Activate  -> aaron/Cursors.cpp:62
// 0x1000896c  ord 9  CursorPair::CursorPair  -> aaron/Cursors.cpp:78
// 0x100089cb  ord 66  CursorPair::~CursorPair  -> aaron/Cursors.cpp:83
// 0x100089e7  ord 598  CursorPair::Handle  -> aaron/Cursors.cpp:89
// 0x10008a83  ord 134  CursorPair::Activate  -> aaron/Cursors.cpp:85
// 0x10008be0  ord 367  Cursor::GetObjClassId  -> aaron/Cursors.h:27   [0x20108]
// 0x10008bf0  ord 131  Cursor::Activate  -> aaron/Cursors.h:6   [returns the id]
// 0x10008c10  ord 599  ResCursor::Handle  -> aaron/Cursors.h:7
// 0x10008d8d  ord 12  RegKey::RegKey_Root  -> aaron/RegKey.h:30
// 0x10008de5  ord 67  RegKey::~RegKey  -> aaron/RegKey.cpp:129
// 0x10008e1e  ord 171  RegKey::OpenSubKey  -> aaron/RegKey.h:39
// 0x10008edd  ord 175  RegKey::DeleteValue  -> aaron/RegKey.cpp:215   [also deletes the sub key of that name (original quirk)]
// 0x10008fdf  ord 251  RegKey::QueryString  -> aaron/RegKey.cpp:227
// 0x100090c2  ord 624  RegKey::SetString  -> aaron/RegKey.h:57
// 0x1000928d  ord 70  Controller::~Controller  -> aaron/Input.h:142   [sources are unique_ptr-owned]
// 0x10009422  ord 185  Controller::SetSourceFlags  -> aaron/Input.cpp:134
// 0x10009481  ord 291  Controller::FindSource  -> aaron/Input.cpp:139   [returns the last source when none matches (original quirk)]
// 0x1000972a  ord 608  Controller::SetBounds_LTRB  -> aaron/Input.cpp:125
// 0x10009ac4  ord 71  InputSource::~InputSource  -> aaron/Input.cpp:225
// 0x10009b27  ord 588  InputSource::Enable  -> aaron/Input.cpp:217
// 0x10009b72  ord 469  InputSource::Reactivate  -> aaron/Input.cpp:107
// 0x10009ce0  ord 75  ControllerSources::~ControllerSources  -> aaron/Input.h:142   [DeleteAll + ~PtrArray: unique_ptr vector in the port]
// 0x10009dd1  ord 197  JoystickSource::PickDevice  -> aaron/Input.cpp:230
// 0x10009e6f  ord 449  JoystickSource::Activate  -> aaron/Input.cpp:240
// 0x1000a100  ord 292  JoystickSource::Type  -> aaron/Input.h:68   [returns 3]
// 0x1000a24c  ord 297  KeyboardSource::FindKey  -> aaron/Input.cpp:320
// 0x1000a28e  ord 450  KeyboardSource::Activate  -> aaron/Input.h:103
// 0x1000a4a9  ord 578  KeyboardSource::Reset  -> aaron/Input.cpp:326
// 0x1000a896  ord 451  MouseSource::Activate  -> aaron/Input.cpp:372
// 0x1000aab0  ord 650  MouseSource::SetBounds_LTRB  -> aaron/Input.h:124
// 0x1000abd0  ord 294  MouseSource::Type  -> aaron/Input.h:121   [returns 2]
// 0x1000afe0  ord 43  Module::Module_Exe  -> aaron/Module.cpp:64
// 0x1000b067  ord 42  Module::Module_Dll  -> aaron/Module.cpp:59
// 0x1000b0e3  ord 41  Module::Module_Fn  -> aaron/Module.cpp:57
// 0x1000b141  ord 74  Module::~Module  -> aaron/Module.cpp:66
// 0x1000b1c4  ord 417  Module::Matches  -> aaron/Module.cpp:71
// 0x1000b213  ord 416  Module::HasCreateFn  -> aaron/Module.h:45   [Ghidra: CMFCToolBar::IsDragButton (false library match)]
// 0x1000b30b  ord 439  Module::BuildPath  -> aaron/Module.cpp:90
// 0x1000b3c9  ord 702  Module::Unload  -> aaron/Module.cpp:83
// 0x1000b460  ord 383  Module::GetClassId  -> aaron/Module.h:41   [0x20083]
// 0x1000b699  ord 205  ResTable::Free_Thunk  -> aaron/ResArchive.h:48
// 0x1000b6b9  ord 352  ResTable::GetLastLoad  -> aaron/ResArchive.cpp:105
// 0x1000b6d3  ord 427  ResTable::Load_Thunk  -> aaron/ResArchive.h:47
// 0x1000b75d  ord 580  ResTable::ResetLastLoad  -> aaron/ResArchive.cpp:106
// 0x1000b797  ord 202  Scene::FreeDynamicResource  -> aaron/SceneRes.cpp:39
// 0x1000ba17  ord 192  ResTable::ForEachSlot  -> aaron/ResArchive.cpp:78
// 0x1000ba76  ord 193  ResTable::ForEachResource  -> aaron/ResArchive.cpp:84
// 0x1000c160  ord 385  ResArchive::GetClassId  -> aaron/ResArchive.h:22   [0x20084]
// 0x1000cbf2  ord 179  Scene::PickWidget  -> aaron/Scene.cpp:43
// 0x1000cc1a  ord 184  Scene::EnableCount  -> aaron/Scene.cpp:44
// 0x1000cc39  ord 186  Scene::SuspendControllers  -> aaron/Scene.h:166
// 0x1000cc88  ord 198  Scene::GetIndexedResObject  -> aaron/Scene.cpp:55
// 0x1000ccd6  ord 199  Scene::GetResObject  -> aaron/Scene.cpp:47
// 0x1000cd6d  ord 338  Scene::GetResType  -> aaron/Scene.cpp:61
// 0x1000ce39  ord 392  Scene::GetUserKey  -> aaron/Scene.cpp:116
// 0x1000cf9c  ord 413  Scene::ContainsWidget  -> aaron/Scene.cpp:69
// 0x1000d04d  ord 440  Scene::ToScreen  -> aaron/Scene.cpp:74
// 0x1000d09d  ord 441  Scene::FromScreen  -> aaron/Scene.cpp:81
// 0x1000d0ed  ord 446  Scene::MessageBox  -> aaron/Scene.cpp:88
// 0x1000dc96  ord 532  Scene::HideCount  -> aaron/Scene.cpp:45
// 0x1000dcb5  ord 536  Scene::PlaySoundInfo  -> aaron/SceneRes.cpp:129
// 0x1000dde7  ord 537  Scene::PopAccelerators  -> aaron/Managers.cpp:127
// 0x1000de5a  ord 556  Scene::PushAccelerators  -> aaron/Managers.cpp:118
// 0x1000dfb8  ord 622  Scene::SetRegistryPath  -> aaron/Scene.cpp:98
// 0x1000dfd9  ord 653  Scene::SetPaletteRes  -> aaron/Scene.cpp:100
// 0x1000e350  ord 386  Scene::GetObjClassId  -> aaron/Scene.h:50   [0x20082]
// 0x1000e360  ord 293,489,508,517,518  Scene::OnPreCreate  -> aaron/Application.h:76   [shared "return 1" thunk (ords 293/489/508/517/518: OnPreCreate/OnCreate/OnStart, Keyboard type, OnQueryClose)]
// 0x1000e370  ord 475  Scene::OnQuery  -> aaron/Scene.h:69
// 0x1000e380  ord 492  Scene::OnInput  -> aaron/Scene.h:70
// 0x1000e3a0  ord 519  Scene::OnObjectEvent2  -> aaron/Scene.h:76
// 0x1000e750  ord 50  Window::Window  -> aaron/Window.cpp:18
// 0x1000e782  ord 79  Window::~Window  -> aaron/Window.cpp:20
// 0x1000e7d4  ord 165  Window::CreateInParent  -> aaron/Window.cpp:92   [SDL: 640x480 platform window; recorded geometry only]
// 0x1000e829  ord 166  Window::Create  -> aaron/Window.cpp:83
// 0x1000e8f4  ord 285  Window::GetClientHeight  -> aaron/Window.cpp:62
// 0x1000e9c0  ord 395  Window::GetClientWidth  -> aaron/Window.cpp:61
// 0x1000ea12  ord 400  Window::WindowProc  -> aaron/Window.cpp:197   [Win32 messages mapped onto Scene/FocusMgr/CursorMgr]
// 0x1000eeb4  ord 445  Window::SetPos  -> aaron/Window.cpp:100   [recorded in the placement]
// 0x1000efea  ord 542  Window::PostMessage  -> aaron/Window.cpp:114   [queue drained by PumpPlatformEvents]
// 0x1000f010  ord 572  Window::Invalidate  -> aaron/Window.cpp:136
// 0x1000f041  ord 583  Window::ShowPlacement  -> aaron/Window.cpp:162   [placement stored in the RegKey as binary "Placement"]
// 0x1000f0b2  ord 585  Window::SavePlacement  -> aaron/Window.cpp:174
// 0x1000f0f2  ord 593  Window::SendMessage  -> aaron/Window.cpp:118
// 0x1000f118  ord 638  Window::ActivateExisting  -> aaron/Window.cpp:189   [only windows of this process (no FindWindow across processes)]
// 0x1000f173  ord 641  Window::SetIcon  -> aaron/Window.cpp:185   [no Win32 icon resources: hook LoadIconResource]
// 0x1000f1d4  ord 667  Window::SetTitleText  -> aaron/Window.cpp:179
// 0x1000f1fb  ord 672  Window::Attach  -> aaron/Window.cpp:64   [handle is a token (SDL has one platform window)]
// 0x1000f217  ord 691  Window::Resize  -> aaron/Window.cpp:108
// 0x1000f24a  ord 687  Window::Show  -> aaron/Window.cpp:141   [SDL window visibility belongs to the platform layer]
// 0x1000f2b0  ord 387  Window::GetClassId  -> aaron/Types.h:54   [0x20081]
// 0x1000f2c0  ord 522  Window::OnSize  -> aaron/Application.h:50   [empty virtual slot 2 (Window and Application)]
// 0x1000f486  ord 53  Application::Application_TitleDll  -> aaron/Application.cpp:50
// 0x1000f667  ord 54  Application::Application_TitleFn  -> aaron/Application.cpp:59
// 0x1000fba4  ord 148  Application::SetStatusTitle  -> aaron/Application.cpp:265
// 0x1000ffa7  ord 168  Application::GetModuleForFn  -> aaron/Application.cpp:239   [Ghidra's CMFCToolBarButton parameter is a false library match]
// 0x1001008e  ord 170  Application::AcquireArchive  -> aaron/Application.h:140
// 0x10010215  ord 194  Application::ForEachWindow  -> aaron/Application.cpp:211
// 0x1001023c  ord 195  Application::ForEachScene  -> aaron/Application.cpp:218
// 0x10010338  ord 393  Application::GetUserKey  -> aaron/Application.h:86
// 0x10010c3a  ord 447  Application::MessageBox  -> aaron/Application.cpp:275   [hook, default: stderr + first button]
// 0x100110c7  ord 528  Application::ParseCommandLine  -> aaron/Application.cpp:287
// 0x10011261  ord 543  Application::IsDialogMessage  -> aaron/Application.cpp:227   [original always returns 0; no native dialogs]
// 0x100112cb  ord 573  Application::InvalidateWindows  -> aaron/Application.cpp:232
// 0x1001131a  ord 579  Application::ResetDisplayMode  -> aaron/Application.cpp:237
// 0x100113d6  ord 623  Application::SetRegistryPath  -> aaron/Application.cpp:252
// 0x100113f7  ord 626  Application::SetBaseDir  -> aaron/Application.cpp:256
// 0x10011540  ord 670  Application::SetTitle  -> aaron/Application.cpp:260
// 0x100118e0  ord 388  Application::GetClassId  -> aaron/Application.h:49   [0x20080]
// 0x100118f0  ord 494,502,504  Application::Reserved10  -> aaron/Application.h:71   [shared "return 0" thunk (ords 494/502/504)]
// 0x10011941  ord 496  Application::NotifyApp  -> aaron/Application.cpp:187
// 0x10011dea  ord 533  AudioMgr::SoundPlay  -> aaron/Managers.cpp:492
// 0x10011e25  ord 574  AudioMgr::Reset  -> aaron/Managers.cpp:494
// 0x10011f23  ord 654  AudioMgr::SetPan  -> aaron/Managers.cpp:495
// 0x10011f6d  ord 692  AudioMgr::SoundStop  -> aaron/Managers.cpp:502
// 0x10011f93  ord 695  AudioMgr::StopAll  -> aaron/Managers.cpp:492
// 0x10012167  ord 136  PickMgr::Hold  -> aaron/Managers.cpp:419
// 0x100121ca  ord 152  PickMgr::OnWidgetDestroyed  -> aaron/Managers.h:264
// 0x100121ec  ord 181  PickMgr::SetEnabled  -> aaron/Managers.cpp:431
// 0x10012283  ord 330  PickMgr::PickEnabled  -> aaron/Managers.cpp:414
// 0x10012355  ord 560  PickMgr::RefreshPointer  -> aaron/Managers.cpp:444   [no-op: SDL reports the pointer continuously]
// 0x1001236b  ord 609  PickMgr::SetHover  -> aaron/Managers.cpp:446
// 0x10012403  ord 570  PickMgr::Release  -> aaron/Managers.cpp:425
// 0x100125b1  ord 642  IdleTimer::SetTimeout  -> aaron/Managers.cpp:75
// 0x100128e1  ord 295  JoystickDev::GetRange  -> aaron/Joystick.cpp:24   [SDL2 platform/Joystick]
// 0x10012995  ord 408  JoystickDev::Probe  -> aaron/Joystick.cpp:22
// 0x10012a59  ord 498  JoystickDev::Dispatch  -> aaron/Joystick.cpp:43
// 0x10012a7a  ord 602  JoystickDev::AddListener  -> aaron/Joystick.cpp:33
// 0x10012aa0  ord 699  JoystickDev::RemoveListener  -> aaron/Joystick.cpp:38
// 0x10012ce2  ord 153  FocusMgr::OnWidgetDestroyed  -> aaron/Managers.cpp:237
// 0x10012d7f  ord 200  FocusMgr::SetFocusState  -> aaron/Managers.cpp:199
// 0x10012dd0  ord 499  FocusMgr::OnMessage  -> aaron/Managers.cpp:246
// 0x10012f45  ord 563  FocusMgr::ClearModal  -> aaron/Managers.h:141
// 0x10012f5a  ord 610  FocusMgr::SetModal  -> aaron/Managers.cpp:228
// 0x10012fae  ord 633  FocusMgr::SetFocusWidget  -> aaron/Managers.cpp:212   [window focus assumed (SDL: always)]
// 0x1001303e  ord 636  FocusMgr::SetFocusToWindow  -> aaron/Managers.cpp:198
// 0x10013353  ord 154  CursorMgr::OnWidgetDestroyed  -> aaron/Managers.cpp:354
// 0x10013392  ord 208  CursorMgr::RefreshPointer  -> aaron/Managers.h:215   [no-op: GetCursorPos/SetCursorPos re-post of the pointer position]
// 0x100133b7  ord 286  CursorMgr::GetUserFlag  -> aaron/Managers.cpp:375
// 0x100135c0  ord 500  CursorMgr::OnMessage  -> aaron/Managers.cpp:359
// 0x1001369a  ord 538  CursorMgr::Nop538  -> aaron/Managers.h:212
// 0x100136a5  ord 557  CursorMgr::Nop557  -> aaron/Managers.h:212
// 0x100136b2  ord 564  CursorMgr::ReleaseCapture  -> aaron/Managers.cpp:350
// 0x100136cc  ord 611  CursorMgr::SetCapture  -> aaron/Managers.cpp:338
// 0x100137e7  ord 616  CursorMgr::SetNumber  -> aaron/Managers.cpp:317
// 0x10013812  ord 618  CursorMgr::SetPos  -> aaron/Managers.cpp:384   [no-op in the port (SetCursorPos); documented]
// 0x10013869  ord 639  CursorMgr::SetUserFlag  -> aaron/Managers.cpp:374
// 0x10013f40  ord 62  TimerMgr::~TimerMgr  -> aaron/Managers.h:49   [compiler-generated destructor chain of the Broadcaster+Subscriber classes (C++ destructors)]
// 0x10014270  ord 438  CombinePath  -> aaron/Path.cpp:51
// 0x1001de04  ord 1  kCopyright  -> aaron/Aaron.h:26   [data export: the copyright string]
