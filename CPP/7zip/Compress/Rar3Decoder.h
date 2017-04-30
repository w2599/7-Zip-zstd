// Rar3Decoder.h
// According to unRAR license, this code may not be used to develop
// a program that creates RAR archives

/* This code uses Carryless rangecoder (1999): Dmitry Subbotin : Public domain */

#ifndef __COMPRESS_RAR3_DECODER_H
#define __COMPRESS_RAR3_DECODER_H

#include "../../../C/Ppmd7.h"

#include "../../Common/MyCom.h"

#include "../ICoder.h"

#include "../Common/InBuffer.h"

#include "BitmDecoder.h"
#include "HuffmanDecoder.h"
#include "Rar3Vm.h"

namespace NCompress {
namespace NRar3 {

const UInt32 kWindowSize = 1 << 22;
const UInt32 kWindowMask = (kWindowSize - 1);

const UInt32 kNumReps = 4;
const UInt32 kNumLen2Symbols = 8;
const UInt32 kLenTableSize = 28;
const UInt32 kMainTableSize = 256 + 1 + 1 + 1 + kNumReps + kNumLen2Symbols + kLenTableSize;
const UInt32 kDistTableSize = 60;

const int kNumAlignBits = 4;
const UInt32 kAlignTableSize = (1 << kNumAlignBits) + 1;

const UInt32 kLevelTableSize = 20;

const UInt32 kTablesSizesSum = kMainTableSize + kDistTableSize + kAlignTableSize + kLenTableSize;

class CBitDecoder
{
  UInt32 _value;
  unsigned _bitPos;
public:
  CInBuffer Stream;

  bool Create(UInt32 bufSize) { return Stream.Create(bufSize); }
  void SetStream(ISequentialInStream *inStream) { Stream.SetStream(inStream);}

  void Init()
  {
    Stream.Init();
    _bitPos = 0;
    _value = 0;
  }
  
  bool ExtraBitsWereRead() const
  {
    return (Stream.NumExtraBytes > 4 || _bitPos < (Stream.NumExtraBytes << 3));
  }

  UInt64 GetProcessedSize() const { return Stream.GetProcessedSize() - (_bitPos >> 3); }

  void AlignToByte()
  {
    _bitPos &= ~(unsigned)7;
    _value = _value & ((1 << _bitPos) - 1);
  }
  
  UInt32 GetValue(unsigned numBits)
  {
    if (_bitPos < numBits)
    {
      _bitPos += 8;
      _value = (_value << 8) | Stream.ReadByte();
      if (_bitPos < numBits)
      {
        _bitPos += 8;
        _value = (_value << 8) | Stream.ReadByte();
      }
    }
    return _value >> (_bitPos - numBits);
  }
  
  void MovePos(unsigned numBits)
  {
    _bitPos -= numBits;
    _value = _value & ((1 << _bitPos) - 1);
  }
  
  UInt32 ReadBits(unsigned numBits)
  {
    UInt32 res = GetValue(numBits);
    MovePos(numBits);
    return res;
  }
};

const UInt32 kTopValue = (1 << 24);
const UInt32 kBot = (1 << 15);

struct CRangeDecoder
{
  IPpmd7_RangeDec vt;
  UInt32 Range;
  UInt32 Code;
  UInt32 Low;
  CBitDecoder BitDecoder;
  SRes Res;

public:
  void InitRangeCoder()
  {
    Code = 0;
    Low = 0;
    Range = 0xFFFFFFFF;
    for (int i = 0; i < 4; i++)
      Code = (Code << 8) | BitDecoder.ReadBits(8);
  }

  void Normalize()
  {
    while ((Low ^ (Low + Range)) < kTopValue ||
       Range < kBot && ((Range = (0 - Low) & (kBot - 1)), 1))
    {
      Code = (Code << 8) | BitDecoder.Stream.ReadByte();
      Range <<= 8;
      Low <<= 8;
    }
  }

  CRangeDecoder() throw();
};

struct CFilter: public NVm::CProgram
{
  CRecordVector<Byte> GlobalData;
  UInt32 BlockStart;
  UInt32 BlockSize;
  UInt32 ExecCount;
  
  CFilter(): BlockStart(0), BlockSize(0), ExecCount(0) {}
};

struct CTempFilter: public NVm::CProgramInitState
{
  UInt32 BlockStart;
  UInt32 BlockSize;
  bool NextWindow;
  
  UInt32 FilterIndex;

  CTempFilter()
  {
    // all filters must contain at least FixedGlobal block
    AllocateEmptyFixedGlobal();
  }
};

const int kNumHuffmanBits = 15;

class CDecoder:
  public ICompressCoder,
  public ICompressSetDecoderProperties2,
  public CMyUnknownImp
{
  CRangeDecoder m_InBitStream;
  Byte *_window;
  UInt32 _winPos;
  UInt32 _wrPtr;
  UInt64 _lzSize;
  UInt64 _unpackSize;
  UInt64 _writtenFileSize; // if it's > _unpackSize, then _unpackSize only written
  ISequentialOutStream *_outStream;
  NHuffman::CDecoder<kNumHuffmanBits, kMainTableSize> m_MainDecoder;
  NHuffman::CDecoder<kNumHuffmanBits, kDistTableSize> m_DistDecoder;
  NHuffman::CDecoder<kNumHuffmanBits, kAlignTableSize> m_AlignDecoder;
  NHuffman::CDecoder<kNumHuffmanBits, kLenTableSize> m_LenDecoder;
  NHuffman::CDecoder<kNumHuffmanBits, kLevelTableSize> m_LevelDecoder;

  UInt32 _reps[kNumReps];
  UInt32 _lastLength;
  
  Byte m_LastLevels[kTablesSizesSum];

  Byte *_vmData;
  Byte *_vmCode;
  NVm::CVm _vm;
  CRecordVector<CFilter *> _filters;
  CRecordVector<CTempFilter *>  _tempFilters;
  UInt32 _lastFilter;

  bool m_IsSolid;

  bool _lzMode;
  bool _unsupportedFilter;

  UInt32 PrevAlignBits;
  UInt32 PrevAlignCount;

  bool TablesRead;
  bool TablesOK;

  CPpmd7 _ppmd;
  int PpmEscChar;
  bool PpmError;
  
  HRESULT WriteDataToStream(const Byte *data, UInt32 size);
  HRESULT WriteData(const Byte *data, UInt32 size);
  HRESULT WriteArea(UInt32 startPtr, UInt32 endPtr);
  void ExecuteFilter(int tempFilterIndex, NVm::CBlockRef &outBlockRef);
  HRESULT WriteBuf();

  void InitFilters();
  bool AddVmCode(UInt32 firstByte, UInt32 codeSize);
  bool ReadVmCodeLZ();
  bool ReadVmCodePPM();
  
  UInt32 ReadBits(int numBits);

  HRESULT InitPPM();
  int DecodePpmSymbol();
  HRESULT DecodePPM(Int32 num, bool &keepDecompressing);

  HRESULT ReadTables(bool &keepDecompressing);
  HRESULT ReadEndOfBlock(bool &keepDecompressing);
  HRESULT DecodeLZ(bool &keepDecompressing);
  HRESULT CodeReal(ICompressProgressInfo *progress);
  
  bool InputEofError() const { return m_InBitStream.BitDecoder.ExtraBitsWereRead(); }
  bool InputEofError_Fast() const { return (m_InBitStream.BitDecoder.Stream.NumExtraBytes > 2); }

public:
  CDecoder();
  ~CDecoder();

  MY_UNKNOWN_IMP1(ICompressSetDecoderProperties2)

  STDMETHOD(Code)(ISequentialInStream *inStream, ISequentialOutStream *outStream,
      const UInt64 *inSize, const UInt64 *outSize, ICompressProgressInfo *progress);

  STDMETHOD(SetDecoderProperties2)(const Byte *data, UInt32 size);

  void CopyBlock(UInt32 distance, UInt32 len)
  {
    _lzSize += len;
    UInt32 pos = (_winPos - distance - 1) & kWindowMask;
    Byte *window = _window;
    UInt32 winPos = _winPos;
    if (kWindowSize - winPos > len && kWindowSize - pos > len)
    {
      const Byte *src = window + pos;
      Byte *dest = window + winPos;
      _winPos += len;
      do
        *dest++ = *src++;
      while (--len != 0);
      return;
    }
    do
    {
      window[winPos] = window[pos];
      winPos = (winPos + 1) & kWindowMask;
      pos = (pos + 1) & kWindowMask;
    }
    while (--len != 0);
    _winPos = winPos;
  }
  
  void PutByte(Byte b)
  {
    _window[_winPos] = b;
    _winPos = (_winPos + 1) & kWindowMask;
    _lzSize++;
  }


};

}}

#endif
