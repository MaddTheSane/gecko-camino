// 7zFolderOutStream.h

#ifndef __7Z_FOLDER_OUT_STREAM_H
#define __7Z_FOLDER_OUT_STREAM_H

#include "7zIn.h"

#include "../../IStream.h"
#include "../IArchive.h"
#include "../Common/OutStreamWithCRC.h"

namespace NArchive {
namespace N7z {

class CFolderOutStream:
  public ISequentialOutStream,
  public CMyUnknownImp
{
  COutStreamWithCRC *_crcStreamSpec;
  CMyComPtr<ISequentialOutStream> _crcStream;
  const CArchiveDatabaseEx *_db;
  const CBoolVector *_extractStatuses;
  CMyComPtr<IArchiveExtractCallback> _extractCallback;
  UInt32 _ref2Offset;
  UInt32 _startIndex;
  int _currentIndex;
  bool _testMode;
  bool _checkCrc;
  bool _fileIsOpen;
  UInt64 _rem;

  HRESULT OpenFile();
  HRESULT CloseFileAndSetResult(Int32 res);
  HRESULT CloseFileAndSetResult();
  HRESULT ProcessEmptyFiles();
public:
  MY_UNKNOWN_IMP
  
  CFolderOutStream();

  STDMETHOD(Write)(const void *data, UInt32 size, UInt32 *processedSize);

  HRESULT Init(
      const CArchiveDatabaseEx *archiveDatabase,
      UInt32 ref2Offset, UInt32 startIndex,
      const CBoolVector *extractStatuses,
      IArchiveExtractCallback *extractCallback,
      bool testMode, bool checkCrc);
  HRESULT FlushCorrupted(Int32 resultEOperationResult);
  HRESULT WasWritingFinished() const
      { return (_currentIndex == _extractStatuses->Size()) ? S_OK: E_FAIL; }
};

}}

#endif
