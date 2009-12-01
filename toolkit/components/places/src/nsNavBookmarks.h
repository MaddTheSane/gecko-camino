/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
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
 * The Original Code is Places.
 *
 * The Initial Developer of the Original Code is
 * Google Inc.
 * Portions created by the Initial Developer are Copyright (C) 2005
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Brian Ryner <bryner@brianryner.com> (original author)
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

#ifndef nsNavBookmarks_h_
#define nsNavBookmarks_h_

#include "nsINavBookmarksService.h"
#include "nsIAnnotationService.h"
#include "nsITransaction.h"
#include "nsNavHistory.h"
#include "nsNavHistoryResult.h" // need for Int64 hashtable
#include "nsToolkitCompsCID.h"
#include "nsCategoryCache.h"

class nsIOutputStream;

class nsNavBookmarks : public nsINavBookmarksService,
                       public nsINavHistoryObserver,
                       public nsIAnnotationObserver
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSINAVBOOKMARKSSERVICE
  NS_DECL_NSINAVHISTORYOBSERVER
  NS_DECL_NSIANNOTATIONOBSERVER

  nsNavBookmarks();

  /**
   * Obtains the service's object.
   */
  static nsNavBookmarks *GetSingleton();

  /**
   * Initializes the service's object.  This should only be called once.
   */
  nsresult Init();

  // called by nsNavHistory::Init
  static nsresult InitTables(mozIStorageConnection* aDBConn);

  static nsNavBookmarks * GetBookmarksService() {
    if (!gBookmarksService) {
      nsCOMPtr<nsINavBookmarksService> serv =
        do_GetService(NS_NAVBOOKMARKSSERVICE_CONTRACTID);
      NS_ENSURE_TRUE(serv, nsnull);
      NS_ASSERTION(gBookmarksService,
                   "Should have static instance pointer now");
    }
    return gBookmarksService;
  }

  nsresult AddBookmarkToHash(PRInt64 aBookmarkId, PRTime aMinTime);

  nsresult ResultNodeForContainer(PRInt64 aID, nsNavHistoryQueryOptions *aOptions,
                                  nsNavHistoryResultNode **aNode);

  // Find all the children of a folder, using the given query and options.
  // For each child, a ResultNode is created and added to |children|.
  // The results are ordered by folder position.
  nsresult QueryFolderChildren(PRInt64 aFolderId,
                               nsNavHistoryQueryOptions *aOptions,
                               nsCOMArray<nsNavHistoryResultNode> *children);

  // If aFolder is -1, uses the autoincrement id for folder index. Returns
  // the index of the new folder in aIndex, whether it was passed in or
  // generated by autoincrement.
  nsresult CreateContainerWithID(PRInt64 aId, PRInt64 aParent,
                                 const nsACString& aName,
                                 const nsAString& aContractId,
                                 PRBool aIsBookmarkFolder,
                                 PRInt32* aIndex,
                                 PRInt64* aNewFolder);

  /**
   * Determines if we have a real bookmark or not (not a livemark).
   *
   * @param aPlaceId
   *        The place_id of the location to check against.
   * @return true if it's a real bookmark, false otherwise.
   */
  PRBool IsRealBookmark(PRInt64 aPlaceId);

  // Called by History service when quitting.
  nsresult OnQuit();

  nsresult BeginUpdateBatch();
  nsresult EndUpdateBatch();

  PRBool ItemExists(PRInt64 aItemId);

  /**
   * Finalize all internal statements.
   */
  nsresult FinalizeStatements();

private:
  static nsNavBookmarks *gBookmarksService;

  ~nsNavBookmarks();

  nsresult InitRoots();
  nsresult InitDefaults();
  nsresult InitStatements();
  nsresult CreateRoot(mozIStorageStatement* aGetRootStatement,
                      const nsCString& name, PRInt64* aID,
                      PRInt64 aParentID, PRBool* aWasCreated);

  nsresult AdjustIndices(PRInt64 aFolder,
                         PRInt32 aStartIndex, PRInt32 aEndIndex,
                         PRInt32 aDelta);

  /**
   * Calculates number of children for the given folder.
   *
   * @param aFolderId Folder to count children for.
   *
   * @return aFolderCount The number of children in this folder.
   *
   * @throws If folder does not exist.
   */
  nsresult FolderCount(PRInt64 aFolderId, PRInt32 *aFolderCount);

  nsresult GetFolderType(PRInt64 aFolder, nsACString &aType);

  nsresult GetLastChildId(PRInt64 aFolder, PRInt64* aItemId);

  nsCOMPtr<mozIStorageConnection> mDBConn;

  nsString mGUIDBase;
  nsresult GetGUIDBase(nsAString& aGUIDBase);

  PRInt32 mItemCount;

  nsMaybeWeakPtrArray<nsINavBookmarkObserver> mObservers;
  PRInt64 mRoot;
  PRInt64 mBookmarksRoot;
  PRInt64 mTagRoot;
  PRInt64 mUnfiledRoot;

  // personal toolbar folder
  PRInt64 mToolbarFolder;

  // the level of nesting of batches, 0 when no batches are open
  PRInt32 mBatchLevel;

  // true if the outermost batch has an associated transaction that should
  // be committed when our batch level reaches 0 again.
  PRBool mBatchHasTransaction;

  // This stores a mapping from all pages reachable by redirects from bookmarked
  // pages to the bookmarked page. Used by GetBookmarkedURIFor.
  nsDataHashtable<nsTrimInt64HashKey, PRInt64> mBookmarksHash;
  nsDataHashtable<nsTrimInt64HashKey, PRInt64>* GetBookmarksHash();
  nsresult FillBookmarksHash();
  nsresult RecursiveAddBookmarkHash(PRInt64 aBookmarkId, PRInt64 aCurrentSource,
                                    PRTime aMinTime);
  nsresult UpdateBookmarkHashOnRemove(PRInt64 aPlaceId);

  nsresult GetParentAndIndexOfFolder(PRInt64 aFolder, PRInt64* aParent, 
                                     PRInt32* aIndex);

  nsresult IsBookmarkedInDatabase(PRInt64 aBookmarkID, PRBool* aIsBookmarked);

  nsresult SetItemDateInternal(mozIStorageStatement* aStatement, PRInt64 aItemId, PRTime aValue);

  // Structure to hold folder's children informations
  struct folderChildrenInfo
  {
    PRInt64 itemId;
    PRUint16 itemType;
    PRInt64 placeId;
    PRInt64 parentId;
    PRInt64 grandParentId;
    PRInt32 index;
    nsCString url;
    nsCString folderType;
  };

  // Recursive method to build an array of folder's children
  nsresult GetDescendantChildren(PRInt64 aFolderId,
                                 PRInt64 aGrandParentId,
                                 nsTArray<folderChildrenInfo>& aFolderChildrenArray);

  enum ItemType {
    BOOKMARK = TYPE_BOOKMARK,
    FOLDER = TYPE_FOLDER,
    SEPARATOR = TYPE_SEPARATOR,
    DYNAMIC_CONTAINER = TYPE_DYNAMIC_CONTAINER
  };

  /**
   * Helper to insert a bookmark in the database.
   *
   *  @param aItemId
   *         The itemId to insert, pass -1 to generate a new one.
   *  @param aPlaceId
   *         The placeId to which this bookmark refers to, pass nsnull for
   *         items that don't refer to an URI (eg. folders, separators, ...).
   *  @param aItemType
   *         The type of the new bookmark, see TYPE_* constants.
   *  @param aParentId
   *         The itemId of the parent folder.
   *  @param aIndex
   *         The position inside the parent folder.
   *  @param aTitle
   *         The title for the new bookmark.
   *         Pass a void string to set a NULL title.
   *  @param aDateAdded
   *         The date for the insertion.
   *  @param [optional] aLastModified
   *         The last modified date for the insertion.
   *         It defaults to aDateAdded.
   *  @param [optional] aServiceContractId
   *         The contract id for a dynamic container.
   *         Pass EmptyCString() for other type of containers.
   *
   *  @return The new item id that has been inserted.
   *
   *  @note This will also update last modified date of the parent folder.
   */
  nsresult InsertBookmarkInDB(PRInt64 aItemId,
                              PRInt64 aPlaceId,
                              enum ItemType aItemType,
                              PRInt64 aParentId,
                              PRInt32 aIndex,
                              const nsACString &aTitle,
                              PRTime aDateAdded,
                              PRTime aLastModified,
                              const nsAString &aServiceContractId,
                              PRInt64 *_retval);

  // kGetInfoIndex_* results + kGetChildrenIndex_* results
  nsCOMPtr<mozIStorageStatement> mDBGetChildren;
  static const PRInt32 kGetChildrenIndex_Position;
  static const PRInt32 kGetChildrenIndex_Type;
  static const PRInt32 kGetChildrenIndex_PlaceID;
  static const PRInt32 kGetChildrenIndex_FolderTitle;
  static const PRInt32 kGetChildrenIndex_ServiceContractId;

  nsCOMPtr<mozIStorageStatement> mDBFindURIBookmarks;  // kFindBookmarksIndex_* results
  static const PRInt32 kFindBookmarksIndex_ID;
  static const PRInt32 kFindBookmarksIndex_Type;
  static const PRInt32 kFindBookmarksIndex_PlaceID;
  static const PRInt32 kFindBookmarksIndex_Parent;
  static const PRInt32 kFindBookmarksIndex_Position;
  static const PRInt32 kFindBookmarksIndex_Title;

  nsCOMPtr<mozIStorageStatement> mDBFolderCount;

  nsCOMPtr<mozIStorageStatement> mDBGetItemIndex;
  nsCOMPtr<mozIStorageStatement> mDBGetChildAt;

  nsCOMPtr<mozIStorageStatement> mDBGetItemProperties; // kGetItemPropertiesIndex_*
  static const PRInt32 kGetItemPropertiesIndex_ID;
  static const PRInt32 kGetItemPropertiesIndex_URI; // null for folders and separators
  static const PRInt32 kGetItemPropertiesIndex_Title;
  static const PRInt32 kGetItemPropertiesIndex_Position;
  static const PRInt32 kGetItemPropertiesIndex_PlaceID;
  static const PRInt32 kGetItemPropertiesIndex_Parent;
  static const PRInt32 kGetItemPropertiesIndex_Type;
  static const PRInt32 kGetItemPropertiesIndex_ServiceContractId;
  static const PRInt32 kGetItemPropertiesIndex_DateAdded;
  static const PRInt32 kGetItemPropertiesIndex_LastModified;

  nsCOMPtr<mozIStorageStatement> mDBGetItemIdForGUID;
  nsCOMPtr<mozIStorageStatement> mDBGetRedirectDestinations;

  nsCOMPtr<mozIStorageStatement> mDBInsertBookmark;
  static const PRInt32 kInsertBookmarkIndex_Id;
  static const PRInt32 kInsertBookmarkIndex_PlaceId;
  static const PRInt32 kInsertBookmarkIndex_Type;
  static const PRInt32 kInsertBookmarkIndex_Parent;
  static const PRInt32 kInsertBookmarkIndex_Position;
  static const PRInt32 kInsertBookmarkIndex_Title;
  static const PRInt32 kInsertBookmarkIndex_ServiceContractId;
  static const PRInt32 kInsertBookmarkIndex_DateAdded;
  static const PRInt32 kInsertBookmarkIndex_LastModified;

  nsCOMPtr<mozIStorageStatement> mDBIsBookmarkedInDatabase;
  nsCOMPtr<mozIStorageStatement> mDBIsRealBookmark;
  nsCOMPtr<mozIStorageStatement> mDBGetLastBookmarkID;
  nsCOMPtr<mozIStorageStatement> mDBSetItemDateAdded;
  nsCOMPtr<mozIStorageStatement> mDBSetItemLastModified;
  nsCOMPtr<mozIStorageStatement> mDBSetItemIndex;

  // keywords
  nsCOMPtr<mozIStorageStatement> mDBGetKeywordForURI;
  nsCOMPtr<mozIStorageStatement> mDBGetKeywordForBookmark;
  nsCOMPtr<mozIStorageStatement> mDBGetURIForKeyword;

  class RemoveFolderTransaction : public nsITransaction {
  public:
    RemoveFolderTransaction(PRInt64 aID) : mID(aID) {}

    NS_DECL_ISUPPORTS

    NS_IMETHOD DoTransaction() {
      nsNavBookmarks* bookmarks = nsNavBookmarks::GetBookmarksService();
      NS_ENSURE_TRUE(bookmarks, NS_ERROR_OUT_OF_MEMORY);
      nsresult rv = bookmarks->GetParentAndIndexOfFolder(mID, &mParent, &mIndex);
      NS_ENSURE_SUCCESS(rv, rv);

      rv = bookmarks->GetItemTitle(mID, mTitle);
      NS_ENSURE_SUCCESS(rv, rv);

      nsCAutoString type;
      rv = bookmarks->GetFolderType(mID, type);
      NS_ENSURE_SUCCESS(rv, rv);
      CopyUTF8toUTF16(type, mType);

      return bookmarks->RemoveFolder(mID);
    }

    NS_IMETHOD UndoTransaction() {
      nsNavBookmarks* bookmarks = nsNavBookmarks::GetBookmarksService();
      NS_ENSURE_TRUE(bookmarks, NS_ERROR_OUT_OF_MEMORY);
      PRInt64 newFolder;
      return bookmarks->CreateContainerWithID(mID, mParent, mTitle, mType, PR_TRUE,
                                              &mIndex, &newFolder); 
    }

    NS_IMETHOD RedoTransaction() {
      return DoTransaction();
    }

    NS_IMETHOD GetIsTransient(PRBool* aResult) {
      *aResult = PR_FALSE;
      return NS_OK;
    }
    
    NS_IMETHOD Merge(nsITransaction* aTransaction, PRBool* aResult) {
      *aResult = PR_FALSE;
      return NS_OK;
    }

  private:
    PRInt64 mID;
    PRInt64 mParent;
    nsCString mTitle;
    nsString mType;
    PRInt32 mIndex;
  };

  // Used to enable and disable the observer notifications.
  bool mCanNotify;
  nsCategoryCache<nsINavBookmarkObserver> mCacheObservers;
};

struct nsBookmarksUpdateBatcher
{
  nsBookmarksUpdateBatcher()
  {
    nsNavBookmarks *bookmarks = nsNavBookmarks::GetBookmarksService();
    if (bookmarks)
      bookmarks->BeginUpdateBatch();
  }
  ~nsBookmarksUpdateBatcher()
  {
    nsNavBookmarks *bookmarks = nsNavBookmarks::GetBookmarksService();
    if (bookmarks)
      bookmarks->EndUpdateBatch();
  }
};


#endif // nsNavBookmarks_h_
