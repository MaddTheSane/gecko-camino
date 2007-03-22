/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=4 sts=2 et cindent: */
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
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 2003
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *  Brian Ryner <bryner@brianryner.com>
 *  Oleg Romashin <romaxa@gmail.com>
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
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

#include "GtkPromptService.h"
#include "EmbedGtkTools.h"
#include "gtkmozembedprivate.h"
#ifndef MOZ_NO_GECKO_UI_FALLBACK_1_8_COMPAT
#include "EmbedPrompter.h"
#endif
#ifdef MOZILLA_INTERNAL_API
#include "nsString.h"
#else
#include "nsStringAPI.h"
#endif
#include "nsIWindowWatcher.h"
#include "nsIWebBrowserChrome.h"
#include "nsIEmbeddingSiteWindow.h"
#include "nsCOMPtr.h"
#include "nsIServiceManager.h"

#define UNACCEPTABLE_CRASHY_GLIB_ALLOCATION(newed) PR_BEGIN_MACRO \
  /* OOPS this code is using a glib allocation function which     \
   * will cause the application to crash when it runs out of      \
   * memory. This is not cool. either g_try methods should be     \
   * used or malloc, or new (note that gecko /will/ be replacing  \
   * its operator new such that new will not throw exceptions).   \
   * XXX please fix me.                                           \
   */                                                             \
  if (!newed) {                                                   \
  }                                                               \
  PR_END_MACRO

GtkPromptService::GtkPromptService()
{
}

GtkPromptService::~GtkPromptService()
{
}

NS_IMPL_ISUPPORTS2(GtkPromptService, nsIPromptService, nsICookiePromptService)

NS_IMETHODIMP
GtkPromptService::Alert(
  nsIDOMWindow* aParent,
  const PRUnichar* aDialogTitle,
  const PRUnichar* aDialogText)
{
  GtkWidget* parentWidget = GetGtkWidgetForDOMWindow(aParent);
  if (parentWidget && gtk_signal_handler_pending(parentWidget, moz_embed_signals[ALERT], TRUE)) {
    nsCString dialogTitle, dialogText;
    CopyUTF16toUTF8(aDialogTitle, dialogTitle);
    CopyUTF16toUTF8(aDialogText, dialogText);
    gtk_signal_emit(GTK_OBJECT(parentWidget),
      moz_embed_signals[ALERT],
      (const gchar *) dialogTitle.get(), (const gchar *) dialogText.get());
    return NS_OK;
  }
#ifndef MOZ_NO_GECKO_UI_FALLBACK_1_8_COMPAT
  EmbedPrompter prompter;
  prompter.SetTitle(aDialogTitle ? aDialogTitle : NS_LITERAL_STRING("Alert").get());
  prompter.SetMessageText(aDialogText);
  prompter.Create(EmbedPrompter::TYPE_ALERT,
          GetGtkWindowForDOMWindow(aParent));
  prompter.Run();
#endif
  return NS_OK;
}

NS_IMETHODIMP
GtkPromptService::AlertCheck(
  nsIDOMWindow* aParent,
  const PRUnichar* aDialogTitle,
  const PRUnichar* aDialogText,
  const PRUnichar* aCheckMsg,
  PRBool* aCheckValue)
{
  NS_ENSURE_ARG_POINTER(aCheckValue);

  GtkWidget* parentWidget = GetGtkWidgetForDOMWindow(aParent);
  if (parentWidget && gtk_signal_handler_pending(parentWidget, moz_embed_signals[ALERT_CHECK], TRUE)) {
    nsCString dialogTitle, dialogText, checkMsg;
    CopyUTF16toUTF8(aDialogTitle, dialogTitle);
    CopyUTF16toUTF8(aDialogText, dialogText);
    CopyUTF16toUTF8(aCheckMsg, checkMsg);
    gtk_signal_emit(GTK_OBJECT(parentWidget),
                    moz_embed_signals[ALERT_CHECK],
                    dialogTitle.get(), dialogTitle.get(), checkMsg.get(), aCheckValue);
    return NS_OK;
  }
#ifndef MOZ_NO_GECKO_UI_FALLBACK_1_8_COMPAT
  EmbedPrompter prompter;
  prompter.SetTitle(aDialogTitle ? aDialogTitle : NS_LITERAL_STRING("Alert").get());
  prompter.SetMessageText(aDialogText);
  prompter.SetCheckMessage(aCheckMsg);
  prompter.SetCheckValue(*aCheckValue);
  prompter.Create(EmbedPrompter::TYPE_ALERT_CHECK,
                  GetGtkWindowForDOMWindow(aParent));
  prompter.Run();
  prompter.GetCheckValue(aCheckValue);
#endif
  return NS_OK;
}

NS_IMETHODIMP
GtkPromptService::Confirm(
  nsIDOMWindow* aParent,
  const PRUnichar* aDialogTitle,
  const PRUnichar* aDialogText, PRBool* aConfirm)
{
  GtkWidget* parentWidget = GetGtkWidgetForDOMWindow(aParent);
  if (parentWidget && gtk_signal_handler_pending(parentWidget, moz_embed_signals[CONFIRM], TRUE)) {
    nsCString dialogTitle, dialogText;
    CopyUTF16toUTF8(aDialogTitle, dialogTitle);
    CopyUTF16toUTF8(aDialogText, dialogText);
    gtk_signal_emit(GTK_OBJECT(parentWidget),
                    moz_embed_signals[CONFIRM],
                    dialogTitle.get(), dialogText.get(), aConfirm);
    return NS_OK;
  }
#ifndef MOZ_NO_GECKO_UI_FALLBACK_1_8_COMPAT
  EmbedPrompter prompter;
  prompter.SetTitle(aDialogTitle ? aDialogTitle : NS_LITERAL_STRING("Confirm").get());
  prompter.SetMessageText(aDialogText);
  prompter.Create(EmbedPrompter::TYPE_CONFIRM,
                  GetGtkWindowForDOMWindow(aParent));
  prompter.Run();
  prompter.GetConfirmValue(aConfirm);
#endif
  return NS_OK;
}

NS_IMETHODIMP
GtkPromptService::ConfirmCheck(
  nsIDOMWindow* aParent,
  const PRUnichar* aDialogTitle,
  const PRUnichar* aDialogText,
  const PRUnichar* aCheckMsg,
  PRBool* aCheckValue,
  PRBool* aConfirm)
{
  NS_ENSURE_ARG_POINTER(aCheckValue);

  GtkWidget* parentWidget = GetGtkWidgetForDOMWindow(aParent);
  if (parentWidget && gtk_signal_handler_pending(parentWidget, moz_embed_signals[CONFIRM_CHECK], TRUE)) {
    nsCString dialogTitle, dialogText, checkMsg;
    CopyUTF16toUTF8(aDialogTitle, dialogTitle);
    CopyUTF16toUTF8(aDialogText, dialogText);
    CopyUTF16toUTF8(aCheckMsg, checkMsg);
    gtk_signal_emit(GTK_OBJECT(parentWidget),
                    moz_embed_signals[CONFIRM_CHECK],
                    dialogTitle.get(),
                    dialogTitle.get(),
                    checkMsg.get(),
                    aCheckValue,
                    aConfirm);
    return NS_OK;
  }
#ifndef MOZ_NO_GECKO_UI_FALLBACK_1_8_COMPAT
  EmbedPrompter prompter;
  prompter.SetTitle(aDialogTitle ? aDialogTitle : NS_LITERAL_STRING("Confirm").get());
  prompter.SetMessageText(aDialogText);
  prompter.SetCheckMessage(aCheckMsg);
  prompter.SetCheckValue(*aCheckValue);
  prompter.Create(EmbedPrompter::TYPE_CONFIRM_CHECK,
                  GetGtkWindowForDOMWindow(aParent));
  prompter.Run();
  prompter.GetCheckValue(aCheckValue);
  prompter.GetConfirmValue(aConfirm);
#endif
  return NS_OK;
}

NS_IMETHODIMP
GtkPromptService::ConfirmEx(
  nsIDOMWindow* aParent,
  const PRUnichar* aDialogTitle,
  const PRUnichar* aDialogText,
  PRUint32 aButtonFlags,
  const PRUnichar* aButton0Title,
  const PRUnichar* aButton1Title,
  const PRUnichar* aButton2Title,
  const PRUnichar* aCheckMsg,
  PRBool* aCheckValue,
  PRInt32* aRetVal)
{
  GtkWidget* parentWidget = GetGtkWidgetForDOMWindow(aParent);
  if (parentWidget && gtk_signal_handler_pending(parentWidget, moz_embed_signals[CONFIRM_EX], TRUE)) {
    nsCString dialogTitle, dialogText, checkMsg;
    nsCString button0,button1,button2;
    CopyUTF16toUTF8(aDialogTitle, dialogTitle);
    CopyUTF16toUTF8(aDialogText, dialogText);
    CopyUTF16toUTF8(aCheckMsg, checkMsg);
    CopyUTF16toUTF8(aButton0Title, button0);
    CopyUTF16toUTF8(aButton1Title, button1);
    CopyUTF16toUTF8(aButton2Title, button2);
    gtk_signal_emit(GTK_OBJECT(parentWidget),
                    moz_embed_signals[CONFIRM_EX],
                    dialogTitle.get(),
                    dialogTitle.get(),
                    aButtonFlags,
                    button0.get(),
                    button1.get(),
                    button2.get(),
                    checkMsg.get(),
                    aCheckValue,
                    aRetVal);
    return NS_OK;
  }
#ifndef MOZ_NO_GECKO_UI_FALLBACK_1_8_COMPAT
  EmbedPrompter prompter;
  prompter.SetTitle(aDialogTitle ? aDialogTitle : NS_LITERAL_STRING("Confirm").get());
  prompter.SetMessageText(aDialogText);

  nsAutoString button0Label, button1Label, button2Label;
  GetButtonLabel(aButtonFlags, BUTTON_POS_0, aButton0Title, button0Label);
  GetButtonLabel(aButtonFlags, BUTTON_POS_1, aButton1Title, button1Label);
  GetButtonLabel(aButtonFlags, BUTTON_POS_2, aButton2Title, button2Label);
  prompter.SetButtons(button0Label.get(),
                      button1Label.get(),
                      button2Label.get());

  if (aCheckMsg)
    prompter.SetCheckMessage(aCheckMsg);
  if (aCheckValue)
    prompter.SetCheckValue(*aCheckValue);

  prompter.Create(EmbedPrompter::TYPE_UNIVERSAL,
                  GetGtkWindowForDOMWindow(aParent));
  prompter.Run();

  if (aCheckValue)
    prompter.GetCheckValue(aCheckValue);

  prompter.GetButtonPressed(aRetVal);
#endif

  return NS_OK;
}

NS_IMETHODIMP
GtkPromptService::Prompt(
  nsIDOMWindow* aParent,
  const PRUnichar* aDialogTitle,
  const PRUnichar* aDialogText,
  PRUnichar** aValue,
  const PRUnichar* aCheckMsg,
  PRBool* aCheckValue,
  PRBool* aConfirm)
{
  GtkWidget* parentWidget = GetGtkWidgetForDOMWindow(aParent);
  if (parentWidget && gtk_signal_handler_pending(parentWidget, moz_embed_signals[PROMPT], TRUE)) {
    nsCString dialogTitle, dialogText, checkMsg, retValue;
    CopyUTF16toUTF8(aDialogTitle, dialogTitle);
    CopyUTF16toUTF8(aDialogText, dialogText);
    CopyUTF16toUTF8(aCheckMsg, checkMsg);
    CopyUTF16toUTF8(*aValue, retValue);
    gchar * aGValue = g_strdup(retValue.get());
    gtk_signal_emit(GTK_OBJECT(parentWidget),
                    moz_embed_signals[PROMPT],
                    dialogTitle.get(),
                    dialogText.get(),
                    &aGValue,
                    checkMsg.get(),
                    aCheckValue,
                    aConfirm,
                    NULL);
    if (*aConfirm) {
      if (*aValue)
        NS_Free(*aValue);
      retValue.Assign(aGValue);
      *aValue = UTF8ToNewUnicode(retValue);
    }
    g_free(aGValue);
    return NS_OK;
  }
#ifndef MOZ_NO_GECKO_UI_FALLBACK_1_8_COMPAT
  EmbedPrompter prompter;
  prompter.SetTitle(aDialogTitle ? aDialogTitle : NS_LITERAL_STRING("Prompt").get());
  prompter.SetMessageText(aDialogText);
  prompter.SetTextValue(*aValue);
  if (aCheckMsg)
    prompter.SetCheckMessage(aCheckMsg);
  if (aCheckValue)
    prompter.SetCheckValue(*aCheckValue);

  prompter.Create(EmbedPrompter::TYPE_PROMPT,
                  GetGtkWindowForDOMWindow(aParent));
  prompter.Run();
  if (aCheckValue)
    prompter.GetCheckValue(aCheckValue);
  prompter.GetConfirmValue(aConfirm);
  if (*aConfirm) {
    if (*aValue)
      NS_Free(*aValue);
    prompter.GetTextValue(aValue);
  }
#endif
  return NS_OK;
}

NS_IMETHODIMP
GtkPromptService::PromptUsernameAndPassword(
  nsIDOMWindow* aParent,
  const PRUnichar* aDialogTitle,
  const PRUnichar* aDialogText,
  PRUnichar** aUsername,
  PRUnichar** aPassword,
  const PRUnichar* aCheckMsg,
  PRBool* aCheckValue,
  PRBool* aConfirm)
{
  GtkWidget* parentWidget = GetGtkWidgetForDOMWindow(aParent);
  if (parentWidget && gtk_signal_handler_pending(parentWidget, moz_embed_signals[PROMPT_AUTH], TRUE)) {
    nsCString dialogTitle, dialogText, checkMsg, retUsername, retPassword;
    CopyUTF16toUTF8(aDialogTitle, dialogTitle);
    CopyUTF16toUTF8(aDialogText, dialogText);
    CopyUTF16toUTF8(aCheckMsg, checkMsg);
    CopyUTF16toUTF8(*aUsername, retUsername);
    CopyUTF16toUTF8(*aPassword, retPassword);
    gchar * aGUsername = g_strdup(retUsername.get());
    gchar * aGPassword = g_strdup(retPassword.get());

    gtk_signal_emit(GTK_OBJECT(parentWidget),
                    moz_embed_signals[PROMPT_AUTH],
                    dialogTitle.get(), dialogText.get(), &aGUsername,
                    &aGPassword, checkMsg.get(), aCheckValue, aConfirm);

    if (*aConfirm) {
        if (*aUsername)
            NS_Free(*aUsername);
        retUsername.Assign(aGUsername);
        *aUsername = UTF8ToNewUnicode(retUsername);
        if (*aPassword)
            NS_Free(*aPassword);
        retPassword.Assign(aGPassword);
        *aPassword = UTF8ToNewUnicode(retPassword);
    }
    g_free(aGUsername);
    g_free(aGPassword);
    return NS_OK;
  }
#ifndef MOZ_NO_GECKO_UI_FALLBACK_1_8_COMPAT
  EmbedPrompter prompter;
  prompter.SetTitle(aDialogTitle ? aDialogTitle : NS_LITERAL_STRING("Prompt").get());
  prompter.SetMessageText(aDialogText);
  prompter.SetUser(*aUsername);
  prompter.SetPassword(*aPassword);
  if (aCheckMsg)
    prompter.SetCheckMessage(aCheckMsg);
  if (aCheckValue)
    prompter.SetCheckValue(*aCheckValue);

  prompter.Create(EmbedPrompter::TYPE_PROMPT_USER_PASS,
                  GetGtkWindowForDOMWindow(aParent));
  prompter.Run();
  if (aCheckValue)
    prompter.GetCheckValue(aCheckValue);
  prompter.GetConfirmValue(aConfirm);
  if (*aConfirm) {
    if (*aUsername)
      NS_Free(*aUsername);
    prompter.GetUser(aUsername);

    if (*aPassword)
      NS_Free(*aPassword);
    prompter.GetPassword(aPassword);
  }
#endif
  return NS_OK;
}

NS_IMETHODIMP
GtkPromptService::PromptPassword(
  nsIDOMWindow* aParent,
  const PRUnichar* aDialogTitle,
  const PRUnichar* aDialogText,
  PRUnichar** aPassword,
  const PRUnichar* aCheckMsg,
  PRBool* aCheckValue, PRBool* aConfirm)
{
  GtkWidget* parentWidget = GetGtkWidgetForDOMWindow(aParent);
  if (parentWidget && gtk_signal_handler_pending(parentWidget, moz_embed_signals[PROMPT_AUTH], TRUE)) {
    nsCString dialogTitle, dialogText, checkMsg, retPassword;
    CopyUTF16toUTF8(aDialogTitle, dialogTitle);
    CopyUTF16toUTF8(aDialogText, dialogText);
    CopyUTF16toUTF8(aCheckMsg, checkMsg);
    CopyUTF16toUTF8(*aPassword, retPassword);
    gchar * aGPassword = g_strdup(retPassword.get());
    gtk_signal_emit(
      GTK_OBJECT(parentWidget),
      moz_embed_signals[PROMPT_AUTH],
      dialogTitle.get(),
      dialogText.get(),
      NULL,
      &aGPassword,
      checkMsg.get(),
      aCheckValue,
      aConfirm);
    if (*aConfirm) {
      if (*aPassword)
        NS_Free(*aPassword);
      retPassword.Assign(aGPassword);
      *aPassword = UTF8ToNewUnicode(retPassword);
    }
    g_free(aGPassword);
    return NS_OK;
  }
#ifndef MOZ_NO_GECKO_UI_FALLBACK_1_8_COMPAT
  EmbedPrompter prompter;
  prompter.SetTitle(aDialogTitle ? aDialogTitle : NS_LITERAL_STRING("Prompt").get());
  prompter.SetMessageText(aDialogText);
  prompter.SetPassword(*aPassword);
  if (aCheckMsg)
    prompter.SetCheckMessage(aCheckMsg);
  if (aCheckValue)
    prompter.SetCheckValue(*aCheckValue);

  prompter.Create(EmbedPrompter::TYPE_PROMPT_PASS,
                  GetGtkWindowForDOMWindow(aParent));
  prompter.Run();
  if (aCheckValue)
    prompter.GetCheckValue(aCheckValue);
  prompter.GetConfirmValue(aConfirm);
  if (*aConfirm) {
    if (*aPassword)
      NS_Free(*aPassword);
    prompter.GetPassword(aPassword);
  }
#endif
  return NS_OK;
}

NS_IMETHODIMP
GtkPromptService::Select(
  nsIDOMWindow* aParent,
  const PRUnichar* aDialogTitle,
  const PRUnichar* aDialogText,
  PRUint32 aCount,
  const PRUnichar** aSelectList,
  PRInt32* outSelection,
  PRBool* aConfirm)
{
  GtkWidget* parentWidget = GetGtkWidgetForDOMWindow(aParent);
  if (parentWidget && gtk_signal_handler_pending(parentWidget, moz_embed_signals[SELECT], TRUE)) {
    nsCString dialogTitle, dialogText;
    CopyUTF16toUTF8(aDialogTitle, dialogTitle);
    CopyUTF16toUTF8(aDialogText, dialogText);
    GList * list = NULL;
    nsCString *itemList = new nsCString[aCount];
    NS_ENSURE_TRUE(itemList, NS_ERROR_OUT_OF_MEMORY);
    for (PRUint32 i = 0; i < aCount; ++i) {
      CopyUTF16toUTF8(aSelectList[i], itemList[i]);
      list = g_list_append(list, (gpointer)itemList[i].get());
    }
    gtk_signal_emit(
      GTK_OBJECT(parentWidget),
      moz_embed_signals[SELECT],
      dialogTitle.get(),
      dialogText.get(),
      (const GList**)&list,
      outSelection,
      aConfirm);
    delete[] itemList;
    g_list_free(list);
    return NS_OK;
  }
#ifndef MOZ_NO_GECKO_UI_FALLBACK_1_8_COMPAT
  EmbedPrompter prompter;
  prompter.SetTitle(aDialogTitle ? aDialogTitle : NS_LITERAL_STRING("Select").get());
  prompter.SetMessageText(aDialogText);
  prompter.SetItems(aSelectList, aCount);
  prompter.Create(EmbedPrompter::TYPE_SELECT,
                  GetGtkWindowForDOMWindow(aParent));
  prompter.Run();
  prompter.GetSelectedItem(outSelection);
  prompter.GetConfirmValue(aConfirm);
#endif
  return NS_OK;
}
/* nsCookiePromptService */
NS_IMETHODIMP
GtkPromptService::CookieDialog(
  nsIDOMWindow *aParent,
  nsICookie *aCookie,
  const nsACString &aHostname,
  PRInt32 aCookiesFromHost,
  PRBool aChangingCookie,
  PRBool *aRememberDecision,
  PRInt32 *aAccept)
{
  /* FIXME - missing gint actions and gboolean illegal_path */
  gint actions = 1;
  nsCString hostName (aHostname);
  nsCString aName;
  aCookie->GetName(aName);
  nsCString aValue;
  aCookie->GetValue(aValue);
  nsCString aDomain;
  aCookie->GetHost(aDomain);
  nsCString aPath;
  aCookie->GetPath(aPath);
  /* We have to investigate a value to use here */
  gboolean illegal_path = FALSE;
  PRUint64 aExpires;
  aCookie->GetExpires(&aExpires);
  nsCOMPtr<nsIDOMWindow> domWindow (do_QueryInterface (aParent));
  GtkMozEmbed *parentWidget = GTK_MOZ_EMBED(GetGtkWidgetForDOMWindow(domWindow));
  GtkMozEmbedCookie *cookie_struct = g_new0(GtkMozEmbedCookie, 1);
  UNACCEPTABLE_CRASHY_GLIB_ALLOCATION(cookie_struct);
  if (parentWidget && cookie_struct) {
    g_signal_emit_by_name(
      GTK_OBJECT(parentWidget->common),
      "ask-cookie",
      cookie_struct,
      actions,
      (const gchar *) hostName.get (),
      (const gchar *) aName.get(),
      (const gchar *) aValue.get(),
      (const gchar *) aDomain.get(),
      (const gchar *) aPath.get(),
      illegal_path,
      aExpires,
      NULL);
  }
  *aRememberDecision = cookie_struct->remember_decision;
  *aAccept = cookie_struct->accept;
  return NS_OK;
}

#ifndef MOZ_NO_GECKO_UI_FALLBACK_1_8_COMPAT
void
GtkPromptService::GetButtonLabel(
  PRUint32 aFlags,
  PRUint32 aPos,
  const PRUnichar* aStringValue,
  nsAString& aLabel)
{
  PRUint32 posFlag = (aFlags & (255 * aPos)) / aPos;
  switch (posFlag) {
  case 0:
    break;
  case BUTTON_TITLE_OK:
    aLabel.AssignLiteral(GTK_STOCK_OK);
    break;
  case BUTTON_TITLE_CANCEL:
    aLabel.AssignLiteral(GTK_STOCK_CANCEL);
    break;
  case BUTTON_TITLE_YES:
    aLabel.AssignLiteral(GTK_STOCK_YES);
    break;
  case BUTTON_TITLE_NO:
    aLabel.AssignLiteral(GTK_STOCK_NO);
    break;
  case BUTTON_TITLE_SAVE:
    aLabel.AssignLiteral(GTK_STOCK_SAVE);
    break;
  case BUTTON_TITLE_DONT_SAVE:
    aLabel.AssignLiteral("Don't Save");
    break;
  case BUTTON_TITLE_REVERT:
    aLabel.AssignLiteral("Revert");
    break;
  case BUTTON_TITLE_IS_STRING:
    aLabel = aStringValue;
    break;
  default:
    NS_WARNING("Unexpected button flags");
  }
}
#endif
