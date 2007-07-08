/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is mozilla.org code.
 *
 * The Initial Developer of the Original Code is Neil Deakin
 * Portions created by the Initial Developer are Copyright (C) 2006
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either of the GNU General Public License Version 2 or later (the "GPL"),
 * or the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

#include "nsGkAtoms.h"
#include "nsXULPopupManager.h"
#include "nsMenuFrame.h"
#include "nsMenuPopupFrame.h"
#include "nsMenuBarFrame.h"
#include "nsIPopupBoxObject.h"
#include "nsMenuBarListener.h"
#include "nsContentUtils.h"
#include "nsIDOMDocument.h"
#include "nsIDOMNSEvent.h"
#include "nsIDOMNSUIEvent.h"
#include "nsEventDispatcher.h"
#include "nsCSSFrameConstructor.h"
#include "nsLayoutUtils.h"
#include "nsIViewManager.h"
#include "nsILookAndFeel.h"
#include "nsIComponentManager.h"
#include "nsITimer.h"
#include "nsIFocusController.h"
#include "nsIDocShellTreeItem.h"
#include "nsIDocShell.h"
#include "nsPIDOMWindow.h"
#include "nsIInterfaceRequestorUtils.h"
#include "nsIBaseWindow.h"

nsXULPopupManager* nsXULPopupManager::sInstance = nsnull;

nsIContent* nsMenuChainItem::Content()
{
  return mFrame->GetContent();
}

void nsMenuChainItem::SetParent(nsMenuChainItem* aParent)
{
  if (mParent) {
    NS_ASSERTION(mParent->mChild == this, "Unexpected - parent's child not set to this");
    mParent->mChild = nsnull;
  }
  mParent = aParent;
  if (mParent) {
    if (mParent->mChild)
      mParent->mChild->mParent = nsnull;
    mParent->mChild = this;
  }
}

void nsMenuChainItem::Detach(nsMenuChainItem** aRoot)
{
  // If the item has a child, set the child's parent to this item's parent,
  // effectively removing the item from the chain. If the item has no child,
  // just set the parent to null.
  if (mChild) {
    NS_ASSERTION(this != *aRoot, "Unexpected - popup with child at end of chain");
    mChild->SetParent(mParent);
  }
  else {
    // An item without a child should be the first item in the chain, so set
    // the first item pointer, pointed to by aRoot, to the parent.
    NS_ASSERTION(this == *aRoot, "Unexpected - popup with no child not at end of chain");
    *aRoot = mParent;
    SetParent(nsnull);
  }
}

NS_IMPL_ISUPPORTS4(nsXULPopupManager, nsIDOMKeyListener,
                   nsIMenuRollup, nsIRollupListener, nsITimerCallback)

nsXULPopupManager::nsXULPopupManager() :
  mRangeOffset(0),
  mActiveMenuBar(nsnull),
  mCurrentMenu(nsnull),
  mPanels(nsnull),
  mTimerMenu(nsnull)
{
}

nsXULPopupManager::~nsXULPopupManager() 
{
  NS_ASSERTION(!mCurrentMenu && !mPanels, "XUL popups still open");
}

nsresult
nsXULPopupManager::Init()
{
  sInstance = new nsXULPopupManager();
  NS_ENSURE_TRUE(sInstance, NS_ERROR_OUT_OF_MEMORY);
  NS_ADDREF(sInstance);
  return NS_OK;
}

void
nsXULPopupManager::Shutdown()
{
  NS_RELEASE(sInstance);
  sInstance = nsnull;
}

nsXULPopupManager*
nsXULPopupManager::GetInstance()
{
  return sInstance;
}

NS_IMETHODIMP
nsXULPopupManager::Rollup()
{
  if (mCurrentMenu)
    HidePopup(mCurrentMenu->Content(), PR_TRUE, PR_TRUE, PR_TRUE);
  return NS_OK;
}

////////////////////////////////////////////////////////////////////////
NS_IMETHODIMP nsXULPopupManager::ShouldRollupOnMouseWheelEvent(PRBool *aShouldRollup) 
{
  // should rollup only for autocomplete widgets
  // XXXndeakin this should really be something the popup has more control over
  *aShouldRollup = (mCurrentMenu && !mCurrentMenu->Frame()->IsMenu()); 
  return NS_OK;
}

// a menu should not roll up if activated by a mouse activate message (eg. X-mouse)
NS_IMETHODIMP nsXULPopupManager::ShouldRollupOnMouseActivate(PRBool *aShouldRollup) 
{
  *aShouldRollup = PR_FALSE;
  return NS_OK;
}

NS_IMETHODIMP
nsXULPopupManager::GetSubmenuWidgetChain(nsISupportsArray **_retval)
{
  nsresult rv = NS_NewISupportsArray(_retval);
  NS_ENSURE_SUCCESS(rv, rv);
  nsMenuChainItem* item = mCurrentMenu;
  while (item) {
    nsCOMPtr<nsIWidget> widget;
    item->Frame()->GetWidget(getter_AddRefs(widget));
    nsCOMPtr<nsISupports> genericWidget(do_QueryInterface(widget));
    (*_retval)->AppendElement(genericWidget);
    item = item->GetParent();
  }

  return NS_OK;
}

nsIFrame*
nsXULPopupManager::GetFrameOfTypeForContent(nsIContent* aContent,
                                            nsIAtom* aFrameType)
{
  nsIDocument *document = aContent->GetCurrentDoc();
  if (document) {
    nsIPresShell* presShell = document->GetPrimaryShell();
    if (presShell) {
      nsIFrame* frame = presShell->GetPrimaryFrameFor(aContent);
      if (frame && frame->GetType() == aFrameType)
        return frame;
    }
  }

  return nsnull;
}

nsMenuFrame*
nsXULPopupManager::GetMenuFrameForContent(nsIContent* aContent)
{
  return static_cast<nsMenuFrame *>
                    (GetFrameOfTypeForContent(aContent, nsGkAtoms::menuFrame));
}

nsMenuPopupFrame*
nsXULPopupManager::GetPopupFrameForContent(nsIContent* aContent)
{
  return static_cast<nsMenuPopupFrame *>
                    (GetFrameOfTypeForContent(aContent, nsGkAtoms::menuPopupFrame));
}

void
nsXULPopupManager::GetMouseLocation(nsIDOMNode** aNode, PRInt32* aOffset)
{
  *aNode = mRangeParent;
  NS_IF_ADDREF(*aNode);
  *aOffset = mRangeOffset;
}

void
nsXULPopupManager::SetMouseLocation(nsIDOMEvent* aEvent)
{
  nsCOMPtr<nsIDOMNSUIEvent> uiEvent = do_QueryInterface(aEvent);
  NS_ASSERTION(uiEvent, "Expected an nsIDOMNSUIEvent");
  if (uiEvent) {
    uiEvent->GetRangeParent(getter_AddRefs(mRangeParent));
    uiEvent->GetRangeOffset(&mRangeOffset);
  }
  else {
    mRangeParent = nsnull;
    mRangeOffset = 0;
  }
}

void
nsXULPopupManager::SetActiveMenuBar(nsMenuBarFrame* aMenuBar, PRBool aActivate)
{
  if (aActivate)
    mActiveMenuBar = aMenuBar;
  else if (mActiveMenuBar == aMenuBar)
    mActiveMenuBar = nsnull;

  UpdateKeyboardListeners();
}

void
nsXULPopupManager::ShowMenu(nsIContent *aMenu,
                            PRBool aSelectFirstItem,
                            PRBool aAsynchronous)
{
  nsMenuFrame* menuFrame = GetMenuFrameForContent(aMenu);
  if (!menuFrame || !menuFrame->IsMenu())
    return;

  nsMenuPopupFrame* popupFrame =  menuFrame->GetPopup();
  if (!popupFrame || !MayShowPopup(popupFrame))
    return;

  // inherit whether or not we're a context menu from the parent
  PRBool parentIsContextMenu = PR_FALSE;
  PRBool onMenuBar = PR_FALSE;
  PRBool onmenu = menuFrame->IsOnMenu();

  nsIMenuParent* parent = menuFrame->GetMenuParent();
  if (parent && onmenu) {
    parentIsContextMenu = parent->IsContextMenu();
    onMenuBar = parent->IsMenuBar();
  }

  nsAutoString position;
  if (onMenuBar || !onmenu)
    position.AssignLiteral("after_start");
  else
    position.AssignLiteral("end_before");
  popupFrame->InitializePopup(aMenu, position, 0, 0, PR_TRUE);

  if (aAsynchronous) {
    nsCOMPtr<nsIRunnable> event =
      new nsXULPopupShowingEvent(popupFrame->GetContent(), aMenu,
                                 parentIsContextMenu, aSelectFirstItem);
    NS_DispatchToCurrentThread(event);
  }
  else {
    FirePopupShowingEvent(popupFrame->GetContent(), aMenu,
                          popupFrame->PresContext(),
                          parentIsContextMenu, aSelectFirstItem);
  }
}

void
nsXULPopupManager::ShowPopup(nsIContent* aPopup,
                             nsIContent* aAnchorContent,
                             const nsAString& aPosition,
                             PRInt32 aXPos, PRInt32 aYPos,
                             PRBool aIsContextMenu,
                             PRBool aAttributesOverride,
                             PRBool aSelectFirstItem)
{
  nsMenuPopupFrame* popupFrame = GetPopupFrameForContent(aPopup);
  if (!popupFrame || !MayShowPopup(popupFrame))
    return;

  popupFrame->InitializePopup(aAnchorContent, aPosition, aXPos, aYPos,
                              aAttributesOverride);

  FirePopupShowingEvent(aPopup, nsnull, popupFrame->PresContext(),
                        aIsContextMenu, aSelectFirstItem);
}

void
nsXULPopupManager::ShowPopupAtScreen(nsIContent* aPopup,
                                     PRInt32 aXPos, PRInt32 aYPos,
                                     PRBool aIsContextMenu)
{
  nsMenuPopupFrame* popupFrame = GetPopupFrameForContent(aPopup);
  if (!popupFrame || !MayShowPopup(popupFrame))
    return;

  popupFrame->InitializePopupAtScreen(aXPos, aYPos);

  FirePopupShowingEvent(aPopup, nsnull, popupFrame->PresContext(),
                        aIsContextMenu, PR_FALSE);
}

void
nsXULPopupManager::ShowPopupWithAnchorAlign(nsIContent* aPopup,
                                            nsIContent* aAnchorContent,
                                            nsAString& aAnchor,
                                            nsAString& aAlign,
                                            PRInt32 aXPos, PRInt32 aYPos,
                                            PRBool aIsContextMenu)
{
  nsMenuPopupFrame* popupFrame = GetPopupFrameForContent(aPopup);
  if (!popupFrame || !MayShowPopup(popupFrame))
    return;

  popupFrame->InitializePopupWithAnchorAlign(aAnchorContent, aAnchor,
                                             aAlign, aXPos, aYPos);

  FirePopupShowingEvent(aPopup, nsnull, popupFrame->PresContext(),
                        aIsContextMenu, PR_FALSE);
}

void
nsXULPopupManager::ShowPopupCallback(nsIContent* aPopup,
                                     nsMenuPopupFrame* aPopupFrame,
                                     PRBool aIsContextMenu,
                                     PRBool aSelectFirstItem)
{
  // clear these as they are no longer valid
  mRangeParent = nsnull;
  mRangeOffset = 0;

  PRInt32 popupType = aPopupFrame->PopupType();
  PRBool ismenu = (popupType == ePopupTypeMenu);

  nsMenuChainItem* item =
    new nsMenuChainItem(aPopupFrame, aIsContextMenu, ismenu);
  if (!item)
    return;

  // install keyboard event listeners for navigating menus, but only if
  // another menu isn't already open.
  // The ignorekeys attribute may be used to disable adding these event listeners
  // for menus that want to handle their own keyboard events.
  if (ismenu) {
    if (aPopup->AttrValueIs(kNameSpaceID_None, nsGkAtoms::ignorekeys,
                             nsGkAtoms::_true, eCaseMatters))
      item->SetIgnoreKeys(PR_TRUE);

    // if the menu is on a menubar, use the menubar's listener instead
    nsIFrame* parent = aPopupFrame->GetParent();
    if (parent && parent->GetType() == nsGkAtoms::menuFrame) {
      nsMenuFrame* menuFrame = static_cast<nsMenuFrame *>(parent);
      item->SetOnMenuBar(menuFrame->IsOnMenuBar());
    }
  }

  // use a weak frame as the popup will set an open attribute if it is a menu
  nsWeakFrame weakFrame(aPopupFrame);
  PRBool hasChildren = aPopupFrame->ShowPopup(aIsContextMenu, aSelectFirstItem);
  ENSURE_TRUE(weakFrame.IsAlive());

  // popups normally hide when an outside click occurs. Panels may use
  // the noautohide attribute to disable this behaviour. It is expected
  // that the application will hide these popups manually. The tooltip
  // listener will handle closing the tooltip also.
  if (popupType == ePopupTypeTooltip ||
      (popupType == ePopupTypePanel &&
       aPopup->AttrValueIs(kNameSpaceID_None, nsGkAtoms::noautohide,
                            nsGkAtoms::_true, eIgnoreCase))) {
    item->SetParent(mPanels);
    mPanels = item;
  }
  else {
    nsIContent* oldmenu = nsnull;
    if (mCurrentMenu)
      oldmenu = mCurrentMenu->Content();
    item->SetParent(mCurrentMenu);
    mCurrentMenu = item;
    SetCaptureState(oldmenu);
  }

  if (hasChildren) {
    if (aSelectFirstItem) {
      nsMenuFrame* next = GetNextMenuItem(aPopupFrame, nsnull, PR_TRUE);
      aPopupFrame->SetCurrentMenuItem(next);
    }

    if (ismenu)
      UpdateMenuItems(aPopup);
  }
}

void
nsXULPopupManager::HidePopup(nsIContent* aPopup,
                             PRBool aHideChain,
                             PRBool aDeselectMenu,
                             PRBool aAsynchronous)
{
  // remove the popup from the open lists. Just to be safe, check both the
  // menu and panels lists.

  // if the popup is on the panels list, remove it but don't close any other panels
  nsMenuPopupFrame* popupFrame = nsnull;
  PRBool foundPanel = PR_FALSE;
  nsMenuChainItem* item = mPanels;
  while (item) {
    if (item->Content() == aPopup) {
      foundPanel = PR_TRUE;
      popupFrame = item->Frame();
      item->Detach(&mPanels);
      delete item;
      break;
    }
    item = item->GetParent();
  }

  // when removing a menu, all of the child popups must be closed
  nsMenuChainItem* foundMenu = nsnull;
  item = mCurrentMenu;
  while (item) {
    if (item->Content() == aPopup) {
      foundMenu = item;
      break;
    }
    item = item->GetParent();
  }

  PRBool ismenu = PR_FALSE;
  PRBool deselectMenu = PR_FALSE;
  nsCOMPtr<nsIContent> popupToHide, nextPopup, lastPopup;
  if (foundMenu) {
    // at this point, item will be set to the found item in the list. If item
    // is the topmost menu, the one being deleted, then there are no other
    // popups to hide. If item is not the topmost menu, then there are
    // open submenus below it. In this case, we need to make sure that those
    // submenus are closed up first. To do this, we start at mCurrentMenu and
    // close that popup. In synchronous mode, the FirePopupHidingEvent method
    // will be called which in turn calls HidePopupCallback to close up the
    // next popup in the chain. These two methods will be called in sequence
    // recursively to close up all the necessary popups. In asynchronous mode,
    // a similar process occurs except that the FirePopupHidingEvent method is
    // called asynchrounsly. In either case, nextPopup is set to the content
    // node of the next popup to close, and lastPopup is set to the last popup
    // in the chain to close, which will be aPopup
    deselectMenu = aDeselectMenu;
    popupToHide = mCurrentMenu->Content();
    popupFrame = mCurrentMenu->Frame();
    ismenu = mCurrentMenu->IsMenu();

    // unhook the top item from the list
    nsMenuChainItem* todelete = mCurrentMenu;
    mCurrentMenu = mCurrentMenu->GetParent();
    if (todelete)
      todelete->SetParent(nsnull);

    if (mCurrentMenu && (aHideChain || todelete != item))
      nextPopup = mCurrentMenu->Content();

    SetCaptureState(popupToHide);
    delete todelete;

    lastPopup = aHideChain ? nsnull : aPopup;
  }
  else if (foundPanel) {
    popupToHide = aPopup;
  }

  if (popupFrame) {
    if (aAsynchronous) {
      nsCOMPtr<nsIRunnable> event =
        new nsXULPopupHidingEvent(popupToHide, nextPopup, lastPopup,
                                  ismenu, deselectMenu);
        NS_DispatchToCurrentThread(event);
    }
    else {
      FirePopupHidingEvent(popupToHide, nextPopup, lastPopup,
                           popupFrame->PresContext(), ismenu, deselectMenu);
    }
  }
}

void
nsXULPopupManager::HidePopupCallback(nsIContent* aPopup,
                                     nsMenuPopupFrame* aPopupFrame,
                                     nsIContent* aNextPopup,
                                     nsIContent* aLastPopup,
                                     PRBool aIsMenu,
                                     PRBool aDeselectMenu)
{
  if (mCloseTimer) {
    mCloseTimer->Cancel();
    mCloseTimer = nsnull;
    mTimerMenu = nsnull;
  }

  nsWeakFrame weakFrame(aPopupFrame);
  aPopupFrame->HidePopup(aDeselectMenu);
  ENSURE_TRUE(weakFrame.IsAlive());

  // send the popuphidden event synchronously. This event has no default behaviour.
  nsEventStatus status = nsEventStatus_eIgnore;
  nsEvent event(PR_TRUE, NS_XUL_POPUP_HIDDEN);
  nsEventDispatcher::Dispatch(aPopup, aPopupFrame->PresContext(),
                              &event, nsnull, &status);

  // if there are more popups to close, look for the next one
  if (aNextPopup && aPopup != aLastPopup) {
    nsMenuChainItem* foundMenu = nsnull;
    nsMenuChainItem* item = mCurrentMenu;
    while (item) {
      if (item->Content() == aNextPopup) {
        foundMenu = item;
        break;
      }
      item = item->GetParent();
    }

    // continue hiding the chain of popups until the last popup aLastPopup
    // is reached, or until a popup of a different type is reached. This
    // last check is needed so that a menulist inside a non-menu panel only
    // closes the menu and not the panel as well.
    if (foundMenu && (aLastPopup || aIsMenu == foundMenu->IsMenu())) {
      PRBool ismenu = foundMenu->IsMenu();
      nsCOMPtr<nsIContent> popupToHide = item->Content();
      item->Detach(&mCurrentMenu);

      nsCOMPtr<nsIContent> nextPopup;
      if (item->GetParent() && popupToHide != aLastPopup)
        nextPopup = item->GetParent()->Content();

      nsPresContext* presContext = item->Frame()->PresContext();

      SetCaptureState(popupToHide);
      delete item;

      FirePopupHidingEvent(popupToHide, nextPopup, aLastPopup,
                           presContext, ismenu, aDeselectMenu);
    }
  }
}

void
nsXULPopupManager::HidePopupAfterDelay(nsMenuPopupFrame* aPopup)
{
  // Don't close up immediately.
  // Kick off a close timer.
  KillMenuTimer();

  PRInt32 menuDelay = 300;   // ms
  aPopup->PresContext()->LookAndFeel()->
    GetMetric(nsILookAndFeel::eMetric_SubmenuDelay, menuDelay);

  // Kick off the timer.
  mCloseTimer = do_CreateInstance("@mozilla.org/timer;1");
  mCloseTimer->InitWithCallback(this, menuDelay, nsITimer::TYPE_ONE_SHOT);

  // the popup will call PopupDestroyed if it is destroyed, which checks if it
  // is set to mTimerMenu, so it should be safe to keep a reference to it
  mTimerMenu = aPopup;
}

void
nsXULPopupManager::HidePopupsInDocument(nsIDocument* aDocument)
{
  nsMenuChainItem* item = mCurrentMenu;
  while (item) {
    if (item->Content()->GetOwnerDoc() == aDocument)
      item->Frame()->HidePopup(PR_TRUE);
    item = item->GetParent();
  }

  item = mPanels;
  while (item) {
    if (item->Content()->GetOwnerDoc() == aDocument)
      item->Frame()->HidePopup(PR_TRUE);
    item = item->GetParent();
  }
}

void
nsXULPopupManager::ExecuteMenu(nsIContent* aMenu, nsEvent* aEvent)
{
  // When a menuitem is selected to be executed, first hide all the open
  // popups, but don't remove them yet. This is needed when a menu command
  // opens a modal dialog. The views associated with the popups needed to be
  // hidden and the accesibility events fired before the command executes, but
  // the popuphiding/popuphidden events are fired afterwards.
  nsMenuChainItem* item = mCurrentMenu;
  while (item) {
    // if it isn't a <menupopup>, don't close it automatically
    if (!item->IsMenu())
      break;
    nsMenuChainItem* next = item->GetParent();
    item->Frame()->HidePopup(PR_TRUE);
    item = next;
  }

  // Create a trusted event if the triggering event was trusted, or if
  // we're called from chrome code (since at least one of our caller
  // passes in a null event).
  PRBool isTrusted = aEvent ? NS_IS_TRUSTED_EVENT(aEvent) :
                              nsContentUtils::IsCallerChrome();

  PRBool shift = PR_FALSE, control = PR_FALSE, alt = PR_FALSE, meta = PR_FALSE;
  if (aEvent && (aEvent->eventStructType == NS_MOUSE_EVENT ||
                 aEvent->eventStructType == NS_KEY_EVENT ||
                 aEvent->eventStructType == NS_ACCESSIBLE_EVENT)) {
    shift = static_cast<nsInputEvent *>(aEvent)->isShift;
    control = static_cast<nsInputEvent *>(aEvent)->isControl;
    alt = static_cast<nsInputEvent *>(aEvent)->isAlt;
    meta = static_cast<nsInputEvent *>(aEvent)->isMeta;
  }

  nsCOMPtr<nsIRunnable> event =
    new nsXULMenuCommandEvent(aMenu, isTrusted, shift, control, alt, meta);
  NS_DispatchToCurrentThread(event);
}

void
nsXULPopupManager::FirePopupShowingEvent(nsIContent* aPopup,
                                         nsIContent* aMenu,
                                         nsPresContext* aPresContext,
                                         PRBool aIsContextMenu,
                                         PRBool aSelectFirstItem)
{
  nsCOMPtr<nsIPresShell> presShell = aPresContext->PresShell();

  // set the open attribute on the menu first so that templates will generate
  // their content before the popupshowing event fires.
  if (aMenu)
    aMenu->SetAttr(kNameSpaceID_None, nsGkAtoms::open,
                   NS_LITERAL_STRING("true"), PR_TRUE);

  // XXXndeakin (bug 383930)
  //   eventually, the popup events will be a different event type with
  //   additional fields for the anchor node and position and so forth. This
  //   is where those details would be retrieved. This removes the need for
  //   all the globals people keep adding to nsIDOMXULDocument.
  nsEventStatus status = nsEventStatus_eIgnore;
  nsEvent event(PR_TRUE, NS_XUL_POPUP_SHOWING);
  nsEventDispatcher::Dispatch(aPopup, aPresContext, &event, nsnull, &status);

  // it is common to append content to the menu during the popupshowing event.
  // Flush the notifications so that the frames are up to date before showing
  // the popup, otherwise the new frames will reflow after the popup appears,
  // causing the popup to flicker. Frame code always calls this asynchronously,
  // so this should be safe.
  nsIDocument *document = aPopup->GetCurrentDoc();
  if (document)
    document->FlushPendingNotifications(Flush_Layout);

  // get the frame again in case it went away
  nsIFrame* frame = presShell->GetPrimaryFrameFor(aPopup);
  if (frame && frame->GetType() == nsGkAtoms::menuPopupFrame) {
    nsMenuPopupFrame* popupFrame = static_cast<nsMenuPopupFrame *>(frame);
    popupFrame->ClearOpenPending();

    if (status != nsEventStatus_eConsumeNoDefault)
      ShowPopupCallback(aPopup, popupFrame, aIsContextMenu, aSelectFirstItem);
  }
}

void
nsXULPopupManager::FirePopupHidingEvent(nsIContent* aPopup,
                                        nsIContent* aNextPopup,
                                        nsIContent* aLastPopup,
                                        nsPresContext *aPresContext,
                                        PRBool aIsMenu,
                                        PRBool aDeselectMenu)
{
  nsCOMPtr<nsIPresShell> presShell = aPresContext->PresShell();

  nsEventStatus status = nsEventStatus_eIgnore;
  nsEvent event(PR_TRUE, NS_XUL_POPUP_HIDING);
  nsEventDispatcher::Dispatch(aPopup, aPresContext, &event, nsnull, &status);

  // get frame again in case it went away
  nsIFrame* frame = presShell->GetPrimaryFrameFor(aPopup);
  if (frame && frame->GetType() == nsGkAtoms::menuPopupFrame) {
    nsMenuPopupFrame* popupFrame = static_cast<nsMenuPopupFrame *>(frame);
    popupFrame->ClearOpenPending();

    if (status != nsEventStatus_eConsumeNoDefault) {
      HidePopupCallback(aPopup, popupFrame, aNextPopup, aLastPopup,
                        aIsMenu, aDeselectMenu);
    }
  }
}

PRBool
nsXULPopupManager::IsPopupOpenForMenuParent(nsIMenuParent* aMenuParent)
{
  nsMenuChainItem* item = mCurrentMenu;
  while (item) {
    nsIFrame* parent = item->Frame()->GetParent();
    if (parent && parent->GetType() == nsGkAtoms::menuFrame) {
      nsMenuFrame* menuFrame = static_cast<nsMenuFrame *>(parent);
      if (menuFrame->GetMenuParent() == aMenuParent)
        return PR_TRUE;
    }
    item = item->GetParent();
  }

  return PR_FALSE;
}

PRBool
nsXULPopupManager::MayShowPopup(nsMenuPopupFrame* aPopup)
{
  // this will return true if the popup is already in the process of being opened
  if (aPopup->IsOpenPending())
    return PR_FALSE;

  nsCOMPtr<nsISupports> cont = aPopup->PresContext()->GetContainer();
  nsCOMPtr<nsIDocShellTreeItem> dsti = do_QueryInterface(cont);
  if (!dsti)
    return PR_FALSE;

  // chrome shells can always open popups, but other types of shells can only
  // open popups when they are focused
  PRInt32 type = -1;
  if (NS_FAILED(dsti->GetItemType(&type)))
    return PR_FALSE;

  if (type != nsIDocShellTreeItem::typeChrome) {
    nsCOMPtr<nsPIDOMWindow> win = do_GetInterface(dsti);
    if (!win)
      return PR_FALSE;

    // only allow popups in active windows
    PRBool active;
    nsIFocusController* focusController = win->GetRootFocusController();
    focusController->GetActive(&active);
    if (!active)
      return PR_FALSE;

    nsCOMPtr<nsIBaseWindow> baseWin = do_QueryInterface(dsti);
    if (!baseWin)
      return PR_FALSE;

    // only allow popups in visible frames
    PRBool visible;
    baseWin->GetVisibility(&visible);
    if (!visible)
      return PR_FALSE;
  }

  // next, check if the popup is already open
  nsMenuChainItem* item = mCurrentMenu;
  while (item) {
    if (item->Frame() == aPopup)
      return PR_FALSE;
    item = item->GetParent();
  }

  item = mPanels;
  while (item) {
    if (item->Frame() == aPopup)
      return PR_FALSE;
    item = item->GetParent();
  }

  // cannot open a popup that is a submenu of a menupopup that isn't open.
  nsIFrame* parent = aPopup->GetParent();
  if (parent && parent->GetType() == nsGkAtoms::menuFrame) {
    nsMenuFrame* menuFrame = static_cast<nsMenuFrame *>(parent);
    nsIMenuParent* parentPopup = menuFrame->GetMenuParent();
    if (parentPopup && !parentPopup->IsOpen())
      return PR_FALSE;
  }

  return PR_TRUE;
}

void
nsXULPopupManager::PopupDestroyed(nsMenuPopupFrame* aPopup)
{
  // when a popup frame is destroyed, just unhook it from the list of popups
  if (mTimerMenu == aPopup) {
    if (mCloseTimer) {
      mCloseTimer->Cancel();
      mCloseTimer = nsnull;
    }
    mTimerMenu = nsnull;
  }

  nsMenuChainItem* item = mPanels;
  while (item) {
    if (item->Frame() == aPopup) {
      item->Detach(&mPanels);
      delete item;
      break;
    }
    item = item->GetParent();
  }

  nsCOMPtr<nsIContent> oldMenu;
  if (mCurrentMenu)
    oldMenu = mCurrentMenu->Content();

  nsMenuChainItem* menuToDestroy = nsnull;
  item = mCurrentMenu;
  while (item) {
    if (item->Frame() == aPopup) {
      item->Detach(&mCurrentMenu);
      menuToDestroy = item;
      break;
    }
    item = item->GetParent();
  }

  if (menuToDestroy) {
    // menuToDestroy will be set to the item to delete. Iterate through any
    // child menus and destroy them as well, since the parent is going away
    nsIFrame* menuToDestroyFrame = menuToDestroy->Frame();
    item = menuToDestroy->GetChild();
    while (item) {
      nsMenuChainItem* next = item->GetChild();

      // if the popup is a child frame of the menu that was destroyed, unhook
      // it from the list of open menus and inform the popup frame that it
      // should be hidden. Don't bother with the events since the frames are
      // going away. If the child menu is not a child frame, for example, a
      // context menu, use HidePopup instead
      if (nsLayoutUtils::IsProperAncestorFrame(menuToDestroyFrame, item->Frame())) {
        item->Detach(&mCurrentMenu);
        item->Frame()->HidePopup(PR_FALSE);
      }
      else {
        HidePopup(item->Content(), PR_FALSE, PR_FALSE, PR_TRUE);
        break;
      }

      delete item;
      item = next;
    }

    delete menuToDestroy;
  }

  if (oldMenu)
    SetCaptureState(oldMenu);
}

PRBool
nsXULPopupManager::HasContextMenu(nsMenuPopupFrame* aPopup)
{
  nsMenuChainItem* item = mCurrentMenu;
  while (item && item->Frame() != aPopup) {
    if (item->IsContextMenu())
      return PR_TRUE;
    item = item->GetParent();
  }

  return PR_FALSE;
}

void
nsXULPopupManager::SetCaptureState(nsIContent* aOldPopup)
{
  if (mCurrentMenu && aOldPopup == mCurrentMenu->Content())
    return;

  if (mWidget) {
    mWidget->CaptureRollupEvents(this, PR_FALSE, PR_FALSE);
    mWidget = nsnull;
  }

  if (mCurrentMenu) {
    nsMenuPopupFrame* popup = mCurrentMenu->Frame();
    nsCOMPtr<nsIWidget> widget;
    popup->GetWidget(getter_AddRefs(widget));
    if (widget) {
      widget->CaptureRollupEvents(this, PR_TRUE, popup->ConsumeOutsideClicks());
      mWidget = widget;
      popup->AttachedDismissalListener();
    }
  }

  UpdateKeyboardListeners();
}

void
nsXULPopupManager::UpdateKeyboardListeners()
{
  nsCOMPtr<nsIDOMEventTarget> newTarget;
  if (mCurrentMenu) {
    if (!mCurrentMenu->IgnoreKeys())
      newTarget = do_QueryInterface(mCurrentMenu->Content()->GetDocument());
  }
  else if (mActiveMenuBar) {
    newTarget = do_QueryInterface(mActiveMenuBar->GetContent()->GetDocument());
  }

  if (mKeyListener != newTarget) {
    if (mKeyListener) {
      mKeyListener->RemoveEventListener(NS_LITERAL_STRING("keypress"), this, PR_TRUE);
      mKeyListener->RemoveEventListener(NS_LITERAL_STRING("keydown"), this, PR_TRUE);
      mKeyListener->RemoveEventListener(NS_LITERAL_STRING("keyup"), this, PR_TRUE);
      mKeyListener = nsnull;
      nsContentUtils::NotifyInstalledMenuKeyboardListener(PR_FALSE);
    }

    if (newTarget) {
      newTarget->AddEventListener(NS_LITERAL_STRING("keypress"), this, PR_TRUE);
      newTarget->AddEventListener(NS_LITERAL_STRING("keydown"), this, PR_TRUE);
      newTarget->AddEventListener(NS_LITERAL_STRING("keyup"), this, PR_TRUE);
      nsContentUtils::NotifyInstalledMenuKeyboardListener(PR_TRUE);
      mKeyListener = newTarget;
    }
  }
}

void
nsXULPopupManager::UpdateMenuItems(nsIContent* aPopup)
{
  // Walk all of the menu's children, checking to see if any of them has a
  // command attribute. If so, then several attributes must potentially be updated.
 
  nsCOMPtr<nsIDOMDocument> domDoc(do_QueryInterface(aPopup->GetDocument()));
  PRUint32 count = aPopup->GetChildCount();
  for (PRUint32 i = 0; i < count; i++) {
    nsCOMPtr<nsIContent> grandChild = aPopup->GetChildAt(i);

    if (grandChild->NodeInfo()->Equals(nsGkAtoms::menuitem, kNameSpaceID_XUL)) {
      // See if we have a command attribute.
      nsAutoString command;
      grandChild->GetAttr(kNameSpaceID_None, nsGkAtoms::command, command);
      if (!command.IsEmpty()) {
        // We do! Look it up in our document
        nsCOMPtr<nsIDOMElement> commandElt;
        domDoc->GetElementById(command, getter_AddRefs(commandElt));
        nsCOMPtr<nsIContent> commandContent(do_QueryInterface(commandElt));
        if (commandContent) {
          nsAutoString commandValue;
          // The menu's disabled state needs to be updated to match the command.
          if (commandContent->GetAttr(kNameSpaceID_None, nsGkAtoms::disabled, commandValue))
            grandChild->SetAttr(kNameSpaceID_None, nsGkAtoms::disabled, commandValue, PR_TRUE);
          else
            grandChild->UnsetAttr(kNameSpaceID_None, nsGkAtoms::disabled, PR_TRUE);

          // The menu's label, accesskey and checked states need to be updated
          // to match the command. Note that unlike the disabled state if the
          // command has *no* value, we assume the menu is supplying its own.
          if (commandContent->GetAttr(kNameSpaceID_None, nsGkAtoms::label, commandValue))
            grandChild->SetAttr(kNameSpaceID_None, nsGkAtoms::label, commandValue, PR_TRUE);

          if (commandContent->GetAttr(kNameSpaceID_None, nsGkAtoms::accesskey, commandValue))
            grandChild->SetAttr(kNameSpaceID_None, nsGkAtoms::accesskey, commandValue, PR_TRUE);

          if (commandContent->GetAttr(kNameSpaceID_None, nsGkAtoms::checked, commandValue))
            grandChild->SetAttr(kNameSpaceID_None, nsGkAtoms::checked, commandValue, PR_TRUE);
        }
      }
    }
  }
}

// Notify
//
// The item selection timer has fired, we might have to readjust the 
// selected item. There are two cases here that we are trying to deal with:
//   (1) diagonal movement from a parent menu to a submenu passing briefly over
//       other items, and
//   (2) moving out from a submenu to a parent or grandparent menu.
// In both cases, |mTimerMenu| is the menu item that might have an open submenu and
// |mCurrentMenu| is the item the mouse is currently over, which could be none of them.
//
// case (1):
//  As the mouse moves from the parent item of a submenu (we'll call 'A') diagonally into the
//  submenu, it probably passes through one or more sibilings (B). As the mouse passes
//  through B, it becomes the current menu item and the timer is set and mTimerMenu is 
//  set to A. Before the timer fires, the mouse leaves the menu containing A and B and
//  enters the submenus. Now when the timer fires, |mCurrentMenu| is null (!= |mTimerMenu|)
//  so we have to see if anything in A's children is selected (recall that even disabled
//  items are selected, the style just doesn't show it). If that is the case, we need to
//  set the selected item back to A.
//
// case (2);
//  Item A has an open submenu, and in it there is an item (B) which also has an open
//  submenu (so there are 3 menus displayed right now). The mouse then leaves B's child
//  submenu and selects an item that is a sibling of A, call it C. When the mouse enters C,
//  the timer is set and |mTimerMenu| is A and |mCurrentMenu| is C. As the timer fires,
//  the mouse is still within C. The correct behavior is to set the current item to C
//  and close up the chain parented at A.
//
//  This brings up the question of is the logic of case (1) enough? The answer is no,
//  and is discussed in bugzilla bug 29400. Case (1) asks if A's submenu has a selected
//  child, and if it does, set the selected item to A. Because B has a submenu open, it
//  is selected and as a result, A is set to be the selected item even though the mouse
//  rests in C -- very wrong. 
//
//  The solution is to use the same idea, but instead of only checking one level, 
//  drill all the way down to the deepest open submenu and check if it has something 
//  selected. Since the mouse is in a grandparent, it won't, and we know that we can
//  safely close up A and all its children.
//
// The code below melds the two cases together.
//
nsresult
nsXULPopupManager::Notify(nsITimer* aTimer)
{
  if (aTimer == mCloseTimer)
    KillMenuTimer();

  return NS_OK;
}

void
nsXULPopupManager::KillMenuTimer()
{
  if (mCloseTimer && mTimerMenu) {
    mCloseTimer->Cancel();
    mCloseTimer = nsnull;

    if (mTimerMenu->IsOpen())
      HidePopup(mTimerMenu->GetContent(), PR_FALSE, PR_FALSE, PR_TRUE);
  }

  mTimerMenu = nsnull;
}

PRBool
nsXULPopupManager::HandleShortcutNavigation(nsIDOMKeyEvent* aKeyEvent)
{
  if (mCurrentMenu) {
    nsMenuPopupFrame* currentPopup = mCurrentMenu->Frame();

    PRBool action;
    nsMenuFrame* result = currentPopup->FindMenuWithShortcut(aKeyEvent, action);
    if (result) {
      currentPopup->ChangeMenuItem(result, PR_FALSE);
      if (action) {
        nsMenuFrame* menuToOpen = result->Enter();
        if (menuToOpen) {
          nsCOMPtr<nsIContent> content = menuToOpen->GetContent();
          ShowMenu(content, PR_TRUE, PR_FALSE);
        }
      }
      return PR_TRUE;
    }

    return PR_FALSE;
  }

  if (mActiveMenuBar) {
    nsMenuFrame* result = mActiveMenuBar->FindMenuWithShortcut(aKeyEvent);
    if (result) {
      mActiveMenuBar->SetActive(PR_TRUE);
      result->OpenMenu(PR_TRUE);
      return PR_TRUE;
    }
  }

  return PR_FALSE;
}


PRBool
nsXULPopupManager::HandleKeyboardNavigation(PRUint32 aKeyCode)
{
  // navigate up through the open menus, looking for the topmost one
  // in the same hierarchy
  nsMenuChainItem* item = nsnull;
  nsMenuChainItem* nextitem = mCurrentMenu;

  while (nextitem) {
    item = nextitem;
    nextitem = item->GetParent();

    if (nextitem) {
      // stop if the parent isn't a menu
      if (!nextitem->IsMenu())
        break;

      // check to make sure that the parent is actually the parent menu. It won't
      // be if the parent is in a different frame hierarchy, for example, for a
      // context menu opened on another menu.
      nsIMenuParent* expectedParent = static_cast<nsIMenuParent *>(nextitem->Frame());
      nsIFrame* parent = item->Frame()->GetParent();
      if (parent && parent->GetType() == nsGkAtoms::menuFrame) {
        nsMenuFrame* menuFrame = static_cast<nsMenuFrame *>(parent);
        if (menuFrame->GetMenuParent() != expectedParent)
          break;
      }
      else {
        break;
      }
    }
  }

  nsIFrame* itemFrame;
  if (item)
    itemFrame = item->Frame();
  else if (mActiveMenuBar)
    itemFrame = mActiveMenuBar;
  else
    return PR_FALSE;

  nsNavigationDirection theDirection;
  NS_DIRECTION_FROM_KEY_CODE(itemFrame, theDirection, aKeyCode);

  // if a popup is open, first check for navigation within the popup
  if (item && HandleKeyboardNavigationInPopup(item, theDirection))
    return PR_TRUE;

  // no popup handled the key, so check the active menubar, if any
  if (mActiveMenuBar) {
    nsMenuFrame* currentMenu = mActiveMenuBar->GetCurrentMenuItem();
  
    if (NS_DIRECTION_IS_INLINE(theDirection)) {
      nsMenuFrame* nextItem = (theDirection == eNavigationDirection_End) ?
                              GetNextMenuItem(mActiveMenuBar, currentMenu, PR_FALSE) : 
                              GetPreviousMenuItem(mActiveMenuBar, currentMenu, PR_FALSE);
      mActiveMenuBar->ChangeMenuItem(nextItem, PR_TRUE);
      return PR_TRUE;
    }
    else if NS_DIRECTION_IS_BLOCK(theDirection) {
      // Open the menu and select its first item.
      nsCOMPtr<nsIContent> content = currentMenu->GetContent();
      ShowMenu(content, PR_TRUE, PR_FALSE);
      return PR_TRUE;
    }
  }

  return PR_FALSE;
}

PRBool
nsXULPopupManager::HandleKeyboardNavigationInPopup(nsMenuChainItem* item,
                                                   nsNavigationDirection aDir)
{
  nsMenuPopupFrame* popupFrame = item->Frame();
  nsMenuFrame* currentMenu = popupFrame->GetCurrentMenuItem();

  popupFrame->ClearIncrementalString();

  // This method only gets called if we're open.
  if (!currentMenu && NS_DIRECTION_IS_INLINE(aDir)) {
    // We've been opened, but we haven't had anything selected.
    // We can handle End, but our parent handles Start.
    if (aDir == eNavigationDirection_End) {
      nsMenuFrame* nextItem = GetNextMenuItem(popupFrame, nsnull, PR_TRUE);
      if (nextItem) {
        popupFrame->ChangeMenuItem(nextItem, PR_FALSE);
        return PR_TRUE;
      }
    }
    return PR_FALSE;
  }

  PRBool isContainer = PR_FALSE;
  PRBool isOpen = PR_FALSE;
  if (currentMenu) {
    isOpen = currentMenu->IsOpen();
    isContainer = currentMenu->IsMenu();
    if (isOpen) {
      // for an open popup, have the child process the event
      nsMenuChainItem* child = item->GetChild();
      if (child && HandleKeyboardNavigationInPopup(child, aDir))
        return PR_TRUE;
    }
    else if (aDir == eNavigationDirection_End &&
             isContainer && !currentMenu->IsDisabled()) {
      // The menu is not yet open. Open it and select the first item.
      nsCOMPtr<nsIContent> content = currentMenu->GetContent();
      ShowMenu(content, PR_TRUE, PR_FALSE);
      return PR_TRUE;
    }
  }

  // For block progression, we can move in either direction
  if (NS_DIRECTION_IS_BLOCK(aDir) ||
      NS_DIRECTION_IS_BLOCK_TO_EDGE(aDir)) {
    nsMenuFrame* nextItem;

    if (aDir == eNavigationDirection_Before)
      nextItem = GetPreviousMenuItem(popupFrame, currentMenu, PR_TRUE);
    else if (aDir == eNavigationDirection_After)
      nextItem = GetNextMenuItem(popupFrame, currentMenu, PR_TRUE);
    else if (aDir == eNavigationDirection_First)
      nextItem = GetNextMenuItem(popupFrame, nsnull, PR_TRUE);
    else
      nextItem = GetPreviousMenuItem(popupFrame, nsnull, PR_TRUE);

    if (nextItem) {
      popupFrame->ChangeMenuItem(nextItem, PR_FALSE);
      return PR_TRUE;
    }
  }
  else if (currentMenu && isContainer && isOpen) {
    if (aDir == eNavigationDirection_Start) {
      // close a submenu when Left is pressed
      nsMenuPopupFrame* popupFrame = currentMenu->GetPopup();
      if (popupFrame)
        HidePopup(popupFrame->GetContent(), PR_FALSE, PR_FALSE, PR_FALSE);
      return PR_TRUE;
    }
  }

  return PR_FALSE;
}

nsMenuFrame*
nsXULPopupManager::GetNextMenuItem(nsIFrame* aParent,
                                   nsMenuFrame* aStart,
                                   PRBool aIsPopup)
{
  nsIFrame* immediateParent = nsnull;
  nsPresContext* presContext = aParent->PresContext();
  presContext->PresShell()->
    FrameConstructor()->GetInsertionPoint(aParent, nsnull, &immediateParent);
  if (!immediateParent)
    immediateParent = aParent;

  nsIFrame* currFrame = nsnull;
  if (aStart)
    currFrame = aStart->GetNextSibling();
  else 
    currFrame = immediateParent->GetFirstChild(nsnull);
  
  while (currFrame) {
    // See if it's a menu item.
    if (IsValidMenuItem(presContext, currFrame->GetContent(), aIsPopup)) {
      return (currFrame->GetType() == nsGkAtoms::menuFrame) ?
             static_cast<nsMenuFrame *>(currFrame) : nsnull;
    }
    currFrame = currFrame->GetNextSibling();
  }

  currFrame = immediateParent->GetFirstChild(nsnull);

  // Still don't have anything. Try cycling from the beginning.
  while (currFrame && currFrame != aStart) {
    // See if it's a menu item.
    if (IsValidMenuItem(presContext, currFrame->GetContent(), aIsPopup)) {
      return (currFrame->GetType() == nsGkAtoms::menuFrame) ?
             static_cast<nsMenuFrame *>(currFrame) : nsnull;
    }

    currFrame = currFrame->GetNextSibling();
  }

  // No luck. Just return our start value.
  return aStart;
}

nsMenuFrame*
nsXULPopupManager::GetPreviousMenuItem(nsIFrame* aParent,
                                       nsMenuFrame* aStart,
                                       PRBool aIsPopup)
{
  nsIFrame* immediateParent = nsnull;
  nsPresContext* presContext = aParent->PresContext();
  presContext->PresShell()->
    FrameConstructor()->GetInsertionPoint(aParent, nsnull, &immediateParent);
  if (!immediateParent)
    immediateParent = aParent;

  nsFrameList frames(immediateParent->GetFirstChild(nsnull));

  nsIFrame* currFrame = nsnull;
  if (aStart)
    currFrame = frames.GetPrevSiblingFor(aStart);
  else
    currFrame = frames.LastChild();

  while (currFrame) {
    // See if it's a menu item.
    if (IsValidMenuItem(presContext, currFrame->GetContent(), aIsPopup)) {
      return (currFrame->GetType() == nsGkAtoms::menuFrame) ?
             static_cast<nsMenuFrame *>(currFrame) : nsnull;
    }
    currFrame = frames.GetPrevSiblingFor(currFrame);
  }

  currFrame = frames.LastChild();

  // Still don't have anything. Try cycling from the end.
  while (currFrame && currFrame != aStart) {
    // See if it's a menu item.
    if (IsValidMenuItem(presContext, currFrame->GetContent(), aIsPopup)) {
      return (currFrame->GetType() == nsGkAtoms::menuFrame) ?
             static_cast<nsMenuFrame *>(currFrame) : nsnull;
    }

    currFrame = frames.GetPrevSiblingFor(currFrame);
  }

  // No luck. Just return our start value.
  return aStart;
}

PRBool
nsXULPopupManager::IsValidMenuItem(nsPresContext* aPresContext,
                                   nsIContent* aContent,
                                   PRBool aOnPopup)
{
  PRInt32 ns = aContent->GetNameSpaceID();
  nsIAtom *tag = aContent->Tag();
  if (ns == kNameSpaceID_XUL &&
       tag != nsGkAtoms::menu &&
       tag != nsGkAtoms::menuitem)
    return PR_FALSE;

  if (ns == kNameSpaceID_XHTML && (!aOnPopup || tag != nsGkAtoms::option))
    return PR_FALSE;

  PRBool skipNavigatingDisabledMenuItem = PR_TRUE;
  if (aOnPopup) {
    aPresContext->LookAndFeel()->
      GetMetric(nsILookAndFeel::eMetric_SkipNavigatingDisabledMenuItem,
                skipNavigatingDisabledMenuItem);
  }

  return !(skipNavigatingDisabledMenuItem &&
           aContent->AttrValueIs(kNameSpaceID_None, nsGkAtoms::disabled,
                                 nsGkAtoms::_true, eCaseMatters));
}

nsresult
nsXULPopupManager::KeyUp(nsIDOMEvent* aKeyEvent)
{
  aKeyEvent->StopPropagation();
  aKeyEvent->PreventDefault();

  return NS_OK; // I am consuming event
}

nsresult
nsXULPopupManager::KeyDown(nsIDOMEvent* aKeyEvent)
{
  PRInt32 menuAccessKey = -1;

  // If the key just pressed is the access key (usually Alt),
  // dismiss and unfocus the menu.

  nsMenuBarListener::GetMenuAccessKey(&menuAccessKey);
  if (menuAccessKey) {
    PRUint32 theChar;
    nsCOMPtr<nsIDOMKeyEvent> keyEvent = do_QueryInterface(aKeyEvent);
    keyEvent->GetKeyCode(&theChar);

    if (theChar == (PRUint32)menuAccessKey) {
      PRBool ctrl = PR_FALSE;
      if (menuAccessKey != nsIDOMKeyEvent::DOM_VK_CONTROL)
        keyEvent->GetCtrlKey(&ctrl);
      PRBool alt=PR_FALSE;
      if (menuAccessKey != nsIDOMKeyEvent::DOM_VK_ALT)
        keyEvent->GetAltKey(&alt);
      PRBool shift=PR_FALSE;
      if (menuAccessKey != nsIDOMKeyEvent::DOM_VK_SHIFT)
        keyEvent->GetShiftKey(&shift);
      PRBool meta=PR_FALSE;
      if (menuAccessKey != nsIDOMKeyEvent::DOM_VK_META)
        keyEvent->GetMetaKey(&meta);
      if (!(ctrl || alt || shift || meta)) {
        // The access key just went down and no other
        // modifiers are already down.
        Rollup();
      }
    }
  }

  // Since a menu was open, eat the event to keep other event
  // listeners from becoming confused.
  aKeyEvent->StopPropagation();
  aKeyEvent->PreventDefault();
  return NS_OK; // I am consuming event
}

nsresult
nsXULPopupManager::KeyPress(nsIDOMEvent* aKeyEvent)
{
  // Don't check prevent default flag -- menus always get first shot at key events.
  // When a menu is open, the prevent default flag on a keypress is always set, so
  // that no one else uses the key event.

  //handlers shouldn't be triggered by non-trusted events.
  nsCOMPtr<nsIDOMNSEvent> domNSEvent = do_QueryInterface(aKeyEvent);
  PRBool trustedEvent = PR_FALSE;

  if (domNSEvent) {
    domNSEvent->GetIsTrusted(&trustedEvent);
  }

  if (!trustedEvent)
    return NS_OK;

  nsCOMPtr<nsIDOMKeyEvent> keyEvent = do_QueryInterface(aKeyEvent);
  PRUint32 theChar;
  keyEvent->GetKeyCode(&theChar);

  if (theChar == NS_VK_LEFT ||
      theChar == NS_VK_RIGHT ||
      theChar == NS_VK_UP ||
      theChar == NS_VK_DOWN ||
      theChar == NS_VK_HOME ||
      theChar == NS_VK_END) {
    HandleKeyboardNavigation(theChar);
  }
  else if (theChar == NS_VK_ESCAPE) {
    // Pressing Escape hides one level of menus only
    if (mCurrentMenu)
      HidePopup(mCurrentMenu->Content(), PR_FALSE, PR_FALSE, PR_FALSE);
  }
  else if (theChar == NS_VK_TAB) {
    Rollup();
  }
  else if (theChar == NS_VK_ENTER ||
           theChar == NS_VK_RETURN) {
    // If there is a popup open, check if the current item needs to be opened.
    // Otherwise, tell the active menubar, if any, to activate the menu. The
    // Enter method will return a menu if one needs to be opened as a result.
    nsMenuFrame* menuToOpen = nsnull;
    if (mCurrentMenu)
      menuToOpen = mCurrentMenu->Frame()->Enter();
    else if (mActiveMenuBar)
      menuToOpen = mActiveMenuBar->Enter();
    if (menuToOpen) {
      nsCOMPtr<nsIContent> content = menuToOpen->GetContent();
      ShowMenu(content, PR_TRUE, PR_FALSE);
    }
  }
#if !defined(XP_MAC) && !defined(XP_MACOSX)
  else if (theChar == NS_VK_F10) {
    // doesn't matter what modifier keys are down in Non-Mac platform
    // if the menu bar is active and F10 is pressed - deactivate it
    Rollup();
  }
#endif // !XP_MAC && !XP_MACOSX
  else {
    HandleShortcutNavigation(keyEvent);
  }

  aKeyEvent->StopPropagation();
  aKeyEvent->PreventDefault();
  return NS_OK; // I am consuming event
}

static nsPresContext*
GetPresContextFor(nsIContent* aContent)
{
  nsIDocument *document = aContent->GetCurrentDoc();
  if (document) {
    nsIPresShell* presShell = document->GetPrimaryShell();
    if (presShell)
      return presShell->GetPresContext();
  }

  return nsnull;
}

NS_IMETHODIMP
nsXULPopupShowingEvent::Run()
{
  nsXULPopupManager* pm = nsXULPopupManager::GetInstance();
  nsPresContext* context = GetPresContextFor(mPopup);
  if (pm && context) {
    pm->FirePopupShowingEvent(mPopup, mMenu, context,
                              mIsContextMenu, mSelectFirstItem);
  }

  return NS_OK;
}

NS_IMETHODIMP
nsXULPopupHidingEvent::Run()
{
  nsXULPopupManager* pm = nsXULPopupManager::GetInstance();
  nsPresContext* context = GetPresContextFor(mPopup);
  if (pm && context) {
    pm->FirePopupHidingEvent(mPopup, mNextPopup, mLastPopup,
                             context, mIsMenu, mDeselectMenu);
  }

  return NS_OK;
}

NS_IMETHODIMP
nsXULMenuCommandEvent::Run()
{
  nsXULPopupManager* pm = nsXULPopupManager::GetInstance();
  if (!pm)
    return NS_OK;

  // The order of the nsIViewManager and nsIPresShell COM pointers is
  // important below.  We want the pres shell to get released before the
  // associated view manager on exit from this function.
  // See bug 54233.
  // XXXndeakin is this still needed?
  nsMenuFrame* menuFrame = pm->GetMenuFrameForContent(mMenu);
  if (menuFrame) {
    nsPresContext* presContext = menuFrame->PresContext();
    nsCOMPtr<nsIViewManager> kungFuDeathGrip = presContext->GetViewManager();
    nsCOMPtr<nsIPresShell> shell = presContext->PresShell();

    // Deselect ourselves.
    menuFrame->SelectMenu(PR_FALSE);

    nsEventStatus status = nsEventStatus_eIgnore;
    nsXULCommandEvent commandEvent(mIsTrusted, NS_XUL_COMMAND, nsnull);
    commandEvent.isShift = mShift;
    commandEvent.isControl = mControl;
    commandEvent.isAlt = mAlt;
    commandEvent.isMeta = mMeta;
    shell->HandleDOMEventWithTarget(mMenu, &commandEvent, &status);
  }

  pm->Rollup();

  return NS_OK;
}
