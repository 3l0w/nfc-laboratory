/*

  Copyright (c) 2021 Jose Vicente Campos Martinez - <josevcm@gmail.com>

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.

*/

#include <QDebug>
#include <QVector>

#include "ProtocolParser.h"
#include "ProtocolFrame.h"

#define FS 13.56E6

struct Parser
{
   enum Caps
   {
      IS_APDU = 1
   };

   unsigned int frameCount = 0;
   unsigned int frameChain = 0;
   unsigned int lastCommand = 0;
   nfc::NfcFrame lastFrame;

   void reset()
   {
      frameCount = 0;
      frameChain = 0;
      lastCommand = 0;
   }

   ProtocolFrame *parseAPDU(const QString &name, const QByteArray &data)
   {
      int lc = (unsigned char) data[4];

      ProtocolFrame *info = buildFieldInfo(name, data);

      info->appendChild(buildFieldInfo("CLA", data.mid(0, 1)));
      info->appendChild(buildFieldInfo("INS", data.mid(1, 1)));
      info->appendChild(buildFieldInfo("P1", data.mid(2, 1)));
      info->appendChild(buildFieldInfo("P2", data.mid(3, 1)));
      info->appendChild(buildFieldInfo("LC", data.mid(4, 1)));

      if (data.length() > lc + 5)
         info->appendChild(buildFieldInfo("LE", data.mid(data.length() - 1, 1)));

      if (lc > 0)
      {
         QByteArray array = data.mid(5, lc);

         ProtocolFrame *payload = buildFieldInfo("DATA", array);

         payload->appendChild(buildFieldInfo(toString(array)));

         info->appendChild(payload);
      }

      return info;
   }

   ProtocolFrame *buildFrameInfo(const QString &name, int rate, QVariant info, double time, double end, int flags, int type)
   {
      return buildInfo(name, rate, info, time, end, flags | ProtocolFrame::RequestFrame, type);
   }

   ProtocolFrame *buildFrameInfo(int rate, QVariant info, double time, double end, int flags, int type)
   {
      return buildInfo(QString(), rate, info, time, end, flags | ProtocolFrame::ResponseFrame, type);
   }

   ProtocolFrame *buildFieldInfo(const QString &name, QVariant info)
   {
      return buildInfo(name, 0, info, 0, 0, ProtocolFrame::FrameField, 0);
   }

   ProtocolFrame *buildFieldInfo(QVariant info)
   {
      return buildInfo(QString(), 0, info, 0, 0, ProtocolFrame::FieldInfo, 0);
   }

   ProtocolFrame *buildInfo(const QString &name, int rate, QVariant info, double start, double end, int flags, int type)
   {
      QVector<QVariant> values;

      // number
      if (flags & ProtocolFrame::RequestFrame || flags & ProtocolFrame::ResponseFrame)
         values << QVariant::fromValue(frameCount);
      else
         values << QVariant::Invalid;

      // time start
      if (start > 0)
         values << QVariant::fromValue(start);
      else
         values << QVariant::Invalid;

      // time end
      if (end > 0)
         values << QVariant::fromValue(end);
      else
         values << QVariant::Invalid;

      // rate
      if (rate > 0)
         values << QVariant::fromValue(rate);
      else
         values << QVariant::Invalid;

      // type
      if (!name.isEmpty())
         values << QVariant::fromValue(name);
      else
         values << QVariant::Invalid;

      // flags
      values << QVariant::fromValue(flags);

      // content
      values << info;

      return new ProtocolFrame(values, flags, type);
   }

   static bool isApdu(const QByteArray &apdu)
   {
      // all APDUs contains at least CLA INS P1 P2 and LE / LC field
      if (apdu.length() < 5)
         return false;

      // get apdu length
      int lc = apdu[4];

      // APDU length must be at least 5 + lc bytes
      if (apdu.length() < lc + 5)
         return false;

      // APDU length must be up to 6 + lc bytes (extra le byte)
      if (apdu.length() > lc + 6)
         return false;

      return true;
   }

   static QByteArray toByteArray(const nfc::NfcFrame &frame, int from = 0, int length = INT32_MAX)
   {
      QByteArray data;

      if (length > frame.limit())
         length = frame.limit();

      if (from >= 0)
      {
         for (int i = from; i < frame.limit() && length > 0; i++, length--)
         {
            data.append(frame[i]);
         }
      }
      else
      {
         for (int i = frame.limit() + from; i < frame.limit() && length > 0; i++, length--)
         {
            data.append(frame[i]);
         }
      }

      return data;
   }

   static QString toString(const QByteArray &array)
   {
      QString text;

      for (unsigned char value : array)
      {
         if (value >= 0x20 && value <= 0x7f)
            text.append(value);
         else
            text.append(".");
      }

      return "[" + text + "]";
   }
};

struct ParserNfcA : Parser
{
   // FSDI to FSD conversion (frame size)
   constexpr static const int NFCA_FDS[] = {16, 24, 32, 40, 48, 64, 96, 128, 256, 0, 0, 0, 0, 0, 0, 0};

   // Start-up Frame Guard Time (SFGT = (256 x 16 / fc) * 2 ^ SFGI)
   constexpr static const float NFCA_SFGT[] = {0.000302065, 0.00060413, 0.00120826, 0.002416519, 0.004833038, 0.009666077, 0.019332153, 0.038664307, 0.077328614, 0.154657227, 0.309314454, 0.618628909, 1.237257817, 2.474515634, 4.949031268, 9.898062537};

   // Frame waiting time (FWT = (256 x 16 / fc) * 2 ^ FWI)
   constexpr static const float NFCA_FWT[] = {0.000302065, 0.00060413, 0.00120826, 0.002416519, 0.004833038, 0.009666077, 0.019332153, 0.038664307, 0.077328614, 0.154657227, 0.309314454, 0.618628909, 1.237257817, 2.474515634, 4.949031268, 9.898062537};

   ProtocolFrame *parse(unsigned int frameCount, const nfc::NfcFrame &frame)
   {
      this->frameCount = frameCount;

      ProtocolFrame *info = nullptr;

      if (frame.isPollFrame())
      {
         if (frameChain == 0)
         {
            int command = frame[0];

            // Request Command, Type A
            if (command == 0x26)
               info = parseRequestREQA(frame);

               // HALT Command, Type A
            else if (command == 0x50)
               info = parseRequestHLTA(frame);

               // Wake-UP Command, Type A
            else if (command == 0x52)
               info = parseRequestWUPA(frame);

               // Mifare AUTH
            else if (command == 0x60 || command == 0x61)
               info = parseRequestAUTH(frame);

               // Select Command, Type A
            else if (command == 0x93 || command == 0x95 || command == 0x97)
               info = parseRequestSELn(frame);

               // Request for Answer to Select
            else if (command == 0xE0)
               info = parseRequestRATS(frame);

               // Protocol Parameter Selection
            else if ((command & 0xF0) == 0xD0)
               info = parseRequestPPSr(frame);

               // ISO-DEP protocol I-Block
            else if ((command & 0xE2) == 0x02)
               info = parseRequestIBlock(frame);

               // ISO-DEP protocol R-Block
            else if ((command & 0xE6) == 0xA2)
               info = parseRequestRBlock(frame);

               // ISO-DEP protocol S-Block
            else if ((command & 0xC7) == 0xC2)
               info = parseRequestSBlock(frame);

               // Unknown frame...
            else
               info = parseRequestUnknown(frame);

            lastCommand = command;
         }

            // Mifare AUTH, two pass
         else if (frameChain == 0x60 || frameChain == 0x61)
         {
            info = parseRequestAUTH(frame);
         }
      }
      else
      {
         int command = lastCommand;

         // Request Command, Type A
         if (command == 0x26)
            info = parseResponseREQA(frame);

            // HALT Command, Type A
         else if (command == 0x50)
            info = parseResponseHLTA(frame);

            // Wake-UP Command, Type A
         else if (command == 0x52)
            info = parseResponseWUPA(frame);

            // Mifare AUTH
         else if (command == 0x60 || command == 0x61)
            info = parseResponseAUTH(frame);

            // Select Command, Type A
         else if (command == 0x93 || command == 0x95 || command == 0x97)
            info = parseResponseSELn(frame);

            // Request for Answer to Select
         else if (command == 0xE0)
            info = parseResponseRATS(frame);

            // Protocol Parameter Selection
         else if ((command & 0xF0) == 0xD0)
            info = parseResponsePPSr(frame);

            // ISO-DEP protocol I-Block
         else if ((command & 0xE2) == 0x02)
            info = parseResponseIBlock(frame);

            // ISO-DEP protocol R-Block
         else if ((command & 0xE6) == 0xA2)
            info = parseResponseRBlock(frame);

            // ISO-DEP protocol S-Block
         else if ((command & 0xC7) == 0xC2)
            info = parseResponseSBlock(frame);

            // Unknown frame...
         else
            info = parseResponseUnknown(frame);
      }

      lastFrame = frame;

      return info;
   }

   inline ProtocolFrame *parseRequestREQA(const nfc::NfcFrame &frame)
   {
      int flags = frame.hasParityError() ? ProtocolFrame::Flags::ParityError : 0;

      return buildFrameInfo("REQA", frame.frameRate(), toByteArray(frame), frame.timeStart(), frame.timeEnd(), flags, ProtocolFrame::SenseFrame);
   }

   inline ProtocolFrame *parseRequestWUPA(const nfc::NfcFrame &frame)
   {
      int flags = frame.hasParityError() ? ProtocolFrame::Flags::ParityError : 0;

      return buildFrameInfo("WUPA", frame.frameRate(), toByteArray(frame), frame.timeStart(), frame.timeEnd(), flags, ProtocolFrame::SenseFrame);
   }

   inline ProtocolFrame *parseRequestSELn(const nfc::NfcFrame &frame)
   {
      ProtocolFrame *root = nullptr;

      int cmd = frame[0];
      int nvb = frame[1] >> 4;
      int flags = 0;

      flags |= frame.hasCrcError() ? ProtocolFrame::Flags::CrcError : 0;
      flags |= frame.hasParityError() ? ProtocolFrame::Flags::ParityError : 0;

      if (cmd == 0x93)
         root = buildFrameInfo("SEL1", frame.frameRate(), toByteArray(frame), frame.timeStart(), frame.timeEnd(), flags, ProtocolFrame::SelectionFrame);
      else if (cmd == 0x95)
         root = buildFrameInfo("SEL2", frame.frameRate(), toByteArray(frame), frame.timeStart(), frame.timeEnd(), flags, ProtocolFrame::SelectionFrame);
      else if (cmd == 0x97)
         root = buildFrameInfo("SEL3", frame.frameRate(), toByteArray(frame), frame.timeStart(), frame.timeEnd(), flags, ProtocolFrame::SelectionFrame);
      else
         root = buildFrameInfo("SEL?", frame.frameRate(), toByteArray(frame), frame.timeStart(), frame.timeEnd(), flags, ProtocolFrame::SelectionFrame);

      // command detailed info
      root->appendChild(buildFieldInfo("NVB", nvb));

      if (nvb == 7)
      {
         if (frame[2] == 0x88) // cascade tag
         {
            root->appendChild(buildFieldInfo("CT", toByteArray(frame, 2, 1)));
            root->appendChild(buildFieldInfo("UID", toByteArray(frame, 3, 3)));
         }
         else
         {
            root->appendChild(buildFieldInfo("UID", toByteArray(frame, 2, 4)));
         }

         root->appendChild(buildFieldInfo("BCC", toByteArray(frame, 6, 1)));
         root->appendChild(buildFieldInfo("CRC", toByteArray(frame, -2)));
      }

      return root;
   }

   inline ProtocolFrame *parseRequestRATS(const nfc::NfcFrame &frame)
   {
      int par = frame[1];
      int cdi = (par & 0x0F);
      int fsdi = (par >> 4) & 0x0F;
      int flags = 0;

      flags |= frame.hasCrcError() ? ProtocolFrame::Flags::CrcError : 0;
      flags |= frame.hasParityError() ? ProtocolFrame::Flags::ParityError : 0;

      ProtocolFrame *root = buildFrameInfo("RATS", frame.frameRate(), toByteArray(frame), frame.timeStart(), frame.timeEnd(), flags, ProtocolFrame::SelectionFrame);

      if (ProtocolFrame *param = root->appendChild(buildFieldInfo("PARAM", QString("%1 [%2]").arg(par, 2, 16, QChar('0')).arg(par, 8, 2, QChar('0')))))
      {
         param->appendChild(buildFieldInfo(QString("[%1....] FSD max frame size %2").arg(fsdi, 4, 2, QChar('0')).arg(NFCA_FDS[fsdi])));
         param->appendChild(buildFieldInfo(QString("[....%1] CDI logical channel %2").arg(cdi, 4, 2, QChar('0')).arg(cdi)));
      }

      root->appendChild(buildFieldInfo("CRC", toByteArray(frame, -2)));

      return root;
   }

   inline ProtocolFrame *parseRequestHLTA(const nfc::NfcFrame &frame)
   {
      int flags = 0;

      flags |= frame.hasCrcError() ? ProtocolFrame::Flags::CrcError : 0;
      flags |= frame.hasParityError() ? ProtocolFrame::Flags::ParityError : 0;

      ProtocolFrame *root = buildFrameInfo("HLTA", frame.frameRate(), toByteArray(frame), frame.timeStart(), frame.timeEnd(), flags, ProtocolFrame::SenseFrame);

      root->appendChild(buildFieldInfo("CRC", toByteArray(frame, -2)));

      return root;
   }

   inline ProtocolFrame *parseRequestPPSr(const nfc::NfcFrame &frame)
   {
      int pps = frame[0];
      int flags = 0;

      flags |= frame.hasCrcError() ? ProtocolFrame::Flags::CrcError : 0;
      flags |= frame.hasParityError() ? ProtocolFrame::Flags::ParityError : 0;

      ProtocolFrame *root = buildFrameInfo("PPS", frame.frameRate(), toByteArray(frame), frame.timeStart(), frame.timeEnd(), flags, ProtocolFrame::SelectionFrame);

      root->appendChild(buildFieldInfo("CID", pps & 0x0F));
      root->appendChild(buildFieldInfo("PPS0", toByteArray(frame, 1, 1)));

      int pps0 = frame[1];

      if (pps0 & 0x10)
      {
         int pps1 = frame[2];

         if (ProtocolFrame *pps1f = root->appendChild(buildFieldInfo("PPS1", QString("%1 [%2]").arg(pps1, 2, 16, QChar('0')).arg(pps1, 8, 2, QChar('0')))))
         {
            if ((pps1 & 0x0C) == 0x00)
               pps1f->appendChild(buildFieldInfo("[....00..] selected 106 kbps PICC to PCD rate"));
            else if ((pps1 & 0x0C) == 0x04)
               pps1f->appendChild(buildFieldInfo("[....01..] selected 212 kbps PICC to PCD rate"));
            else if ((pps1 & 0x0C) == 0x08)
               pps1f->appendChild(buildFieldInfo("[....10..] selected 424 kbps PICC to PCD rate"));
            else if ((pps1 & 0x0C) == 0x0C)
               pps1f->appendChild(buildFieldInfo("[....11..] selected 848 kbps PICC to PCD rate"));

            if ((pps1 & 0x03) == 0x00)
               pps1f->appendChild(buildFieldInfo("[......00] selected 106 kbps PCD to PICC rate"));
            else if ((pps1 & 0x03) == 0x01)
               pps1f->appendChild(buildFieldInfo("[......01] selected 212 kbps PCD to PICC rate"));
            else if ((pps1 & 0x03) == 0x02)
               pps1f->appendChild(buildFieldInfo("[......10] selected 424 kbps PCD to PICC rate"));
            else if ((pps1 & 0x03) == 0x03)
               pps1f->appendChild(buildFieldInfo("[......11] selected 848 kbps PCD to PICC rate"));
         }
      }

      root->appendChild(buildFieldInfo("CRC", toByteArray(frame, -2)));

      return root;
   }

   inline ProtocolFrame *parseRequestAUTH(const nfc::NfcFrame &frame)
   {
      int flags = frame.hasParityError() ? ProtocolFrame::Flags::ParityError : 0;

      if (frameChain == 0)
      {
         int cmd = frame[0];
         int block = frame[1];

         flags |= frame.hasCrcError() ? ProtocolFrame::Flags::CrcError : 0;

         ProtocolFrame *root = buildFrameInfo(cmd == 0x60 ? "AUTH(A)" : "AUTH(B)", frame.frameRate(), toByteArray(frame), frame.timeStart(), frame.timeEnd(), flags, ProtocolFrame::AuthFrame);

         root->appendChild(buildFieldInfo("BLOCK", block));
         root->appendChild(buildFieldInfo("CRC", toByteArray(frame, -2)));

         frameChain = cmd;

         return root;
      }

      ProtocolFrame *root = buildFrameInfo(frameChain == 0x60 ? "AUTH(A)" : "AUTH(B)", frame.frameRate(), toByteArray(frame), frame.timeStart(), frame.timeEnd(), flags, ProtocolFrame::AuthFrame);

      root->appendChild(buildFieldInfo("TOKEN", toByteArray(frame)));

      frameChain = 0;

      return root;
   }

   inline ProtocolFrame *parseRequestIBlock(const nfc::NfcFrame &frame)
   {
      int pcb = frame[0], offset = 1;
      int flags = 0;

      flags |= frame.hasCrcError() ? ProtocolFrame::Flags::CrcError : 0;
      flags |= frame.hasParityError() ? ProtocolFrame::Flags::ParityError : 0;

      ProtocolFrame *root = buildFrameInfo("I-Block", frame.frameRate(), toByteArray(frame), frame.timeStart(), frame.timeEnd(), flags, ProtocolFrame::InformationFrame);

      if (pcb & 0x08)
         root->appendChild(buildFieldInfo("CID", frame[offset++] & 0x0F));

      if (pcb & 0x04)
         root->appendChild(buildFieldInfo("NAD", frame[offset++] & 0xFF));

      if (offset < frame.limit() - 2)
      {
         QByteArray data = toByteArray(frame, offset, frame.limit() - offset - 2);

         if (isApdu(data))
         {
            flags |= IS_APDU;

            root->appendChild(parseAPDU("APDU", data));
         }
         else
         {
            if (ProtocolFrame *payload = root->appendChild(buildFieldInfo("DATA", data)))
            {
               payload->appendChild(buildFieldInfo(toString(data)));
            }
         }
      }

      root->appendChild(buildFieldInfo("CRC", toByteArray(frame, -2)));

      return root;
   }

   inline ProtocolFrame *parseRequestRBlock(const nfc::NfcFrame &frame)
   {
      ProtocolFrame *root;

      int pcb = frame[0], offset = 1;
      int flags = 0;

      flags |= frame.hasCrcError() ? ProtocolFrame::Flags::CrcError : 0;
      flags |= frame.hasParityError() ? ProtocolFrame::Flags::ParityError : 0;

      if (pcb & 0x10)
         root = buildFrameInfo("R(NACK)", frame.frameRate(), toByteArray(frame), frame.timeStart(), frame.timeEnd(), flags, ProtocolFrame::InformationFrame);
      else
         root = buildFrameInfo("R(ACK)", frame.frameRate(), toByteArray(frame), frame.timeStart(), frame.timeEnd(), flags, ProtocolFrame::InformationFrame);

      if (pcb & 0x08)
         root->appendChild(buildFieldInfo("CID", frame[offset++] & 0x0F));

      if (offset < frame.limit() - 2)
         root->appendChild(buildFieldInfo("INF", toByteArray(frame, offset, frame.limit() - offset - 2)));

      root->appendChild(buildFieldInfo("CRC", toByteArray(frame, -2)));

      return root;
   }

   inline ProtocolFrame *parseRequestSBlock(const nfc::NfcFrame &frame)
   {
      int pcb = frame[0], offset = 1;
      int flags = 0;

      flags |= frame.hasCrcError() ? ProtocolFrame::Flags::CrcError : 0;
      flags |= frame.hasParityError() ? ProtocolFrame::Flags::ParityError : 0;

      ProtocolFrame *root = buildFrameInfo("S-Block", frame.frameRate(), toByteArray(frame), frame.timeStart(), frame.timeEnd(), flags, ProtocolFrame::InformationFrame);

      if (pcb & 0x08)
         root->appendChild(buildFieldInfo("CID", frame[offset++] & 0x0F));

      if (offset < frame.limit() - 2)
         root->appendChild(buildFieldInfo("INF", toByteArray(frame, offset, frame.limit() - offset - 2)));

      root->appendChild(buildFieldInfo("CRC", toByteArray(frame, -2)));

      return root;
   }

   inline ProtocolFrame *parseRequestUnknown(const nfc::NfcFrame &frame)
   {
      int flags = frame.hasParityError() ? ProtocolFrame::Flags::ParityError : 0;

      return buildFrameInfo("(unk)", frame.frameRate(), toByteArray(frame), frame.timeStart(), frame.timeEnd(), flags, 0);
   }

   inline ProtocolFrame *parseResponseREQA(const nfc::NfcFrame &frame)
   {
      int atqv = frame[1] << 8 | frame[0];

      int flags = frame.hasParityError() ? ProtocolFrame::Flags::ParityError : 0;

      ProtocolFrame *root = buildFrameInfo(frame.frameRate(), toByteArray(frame), frame.timeStart(), frame.timeEnd(), flags, ProtocolFrame::SenseFrame);

      if (ProtocolFrame *atqa = root->appendChild(buildFieldInfo("ATQA", QString("%1 [%2]").arg(atqv, 4, 16, QChar('0')).arg(atqv, 16, 2, QChar('0')))))
      {
         // propietary TYPE
         atqa->appendChild(buildFieldInfo(QString("  [....%1........] propietary type %2").arg((atqv >> 8) & 0x0F, 4, 2, QChar('0')).arg((atqv >> 8) & 0x0F, 1, 16, QChar('0'))));

         // check UID size
         if ((atqv & 0xC0) == 0x00)
            atqa->appendChild(buildFieldInfo("  [........00......] single size UID"));
         else if ((atqv & 0xC0) == 0x40)
            atqa->appendChild(buildFieldInfo("  [........01......] double size UID"));
         else if ((atqv & 0xC0) == 0x80)
            atqa->appendChild(buildFieldInfo("  [........10......] triple size UID"));
         else if ((atqv & 0xC0) == 0xC0)
            atqa->appendChild(buildFieldInfo("  [........11......] unknow UID size (reserved)"));

         // check SSD bit
         if ((atqv & 0x1F) == 0x00)
            atqa->appendChild(buildFieldInfo("  [...........00000] bit frame anticollision (Type 1 Tag)"));
         else if ((atqv & 0x1F) == 0x01)
            atqa->appendChild(buildFieldInfo("  [...........00001] bit frame anticollision"));
         else if ((atqv & 0x1F) == 0x02)
            atqa->appendChild(buildFieldInfo("  [...........00010] bit frame anticollision"));
         else if ((atqv & 0x1F) == 0x04)
            atqa->appendChild(buildFieldInfo("  [...........00100] bit frame anticollision"));
         else if ((atqv & 0x1F) == 0x08)
            atqa->appendChild(buildFieldInfo("  [...........01000] bit frame anticollision"));
         else if ((atqv & 0x1F) == 0x10)
            atqa->appendChild(buildFieldInfo("  [...........10000] bit frame anticollision"));
      }

      return root;
   }

   inline ProtocolFrame *parseResponseWUPA(const nfc::NfcFrame &frame)
   {
      return parseResponseREQA(frame);
   }

   inline ProtocolFrame *parseResponseSELn(const nfc::NfcFrame &frame)
   {
      int flags = frame.hasParityError() ? ProtocolFrame::Flags::ParityError : 0;

      ProtocolFrame *root = buildFrameInfo(frame.frameRate(), toByteArray(frame), frame.timeStart(), frame.timeEnd(), flags, ProtocolFrame::SelectionFrame);

      if (frame.limit() == 5)
      {
         if (frame[0] == 0x88) // cascade tag
         {
            root->appendChild(buildFieldInfo("CT", toByteArray(frame, 0, 1)));
            root->appendChild(buildFieldInfo("UID", toByteArray(frame, 1, 3)));
         }
         else
         {
            root->appendChild(buildFieldInfo("UID", toByteArray(frame, 0, 4)));
         }

         root->appendChild(buildFieldInfo("BCC", toByteArray(frame, 4, 1)));
      }
      else if (frame.limit() == 3)
      {
         int sa = frame[0];

         if (ProtocolFrame *sak = root->appendChild(buildFieldInfo("SAK", QString("%1 [%2]").arg(sa, 2, 16, QChar('0')).arg(sa, 8, 2, QChar('0')))))
         {
            if (sa & 0x40)
               sak->appendChild(buildFieldInfo("[.1......] ISO/IEC 18092 (NFC) compliant"));
            else
               sak->appendChild(buildFieldInfo("[.0......] not compliant with 18092 (NFC)"));

            if (sa & 0x20)
               sak->appendChild(buildFieldInfo("[..1.....] ISO/IEC 14443-4 compliant"));
            else
               sak->appendChild(buildFieldInfo("[..0.....] not compliant with ISO/IEC 14443-4"));

            if (sa & 0x04)
               sak->appendChild(buildFieldInfo("[.....1..] UID not complete"));
            else
               sak->appendChild(buildFieldInfo("[.....0..] UID complete"));
         }

         root->appendChild(buildFieldInfo("CRC", toByteArray(frame, 1, 2)));
      }

      return root;
   }

   inline ProtocolFrame *parseResponseRATS(const nfc::NfcFrame &frame)
   {
      int offset = 0;
      int tl = frame[offset++];
      int flags = 0;

      flags |= frame.hasCrcError() ? ProtocolFrame::Flags::CrcError : 0;
      flags |= frame.hasParityError() ? ProtocolFrame::Flags::ParityError : 0;

      ProtocolFrame *root = buildFrameInfo(frame.frameRate(), toByteArray(frame), frame.timeStart(), frame.timeEnd(), flags, ProtocolFrame::SelectionFrame);

      root->appendChild(buildFieldInfo("TL", tl));

      if (ProtocolFrame *ats = root->appendChild(buildFieldInfo("ATS", toByteArray(frame, 1, frame.limit() - 3))))
      {
         if (tl > 0)
         {
            int t0 = frame[offset++];
            int fsci = t0 & 0x0f;

            if (ProtocolFrame *t0f = ats->appendChild(buildFieldInfo("T0", QString("%1 [%2]").arg(t0, 2, 16, QChar('0')).arg(t0, 8, 2, QChar('0')))))
            {
               t0f->appendChild(buildFieldInfo(QString("[....%1] max frame size %2").arg(fsci, 4, 2, QChar('0')).arg(NFCA_FDS[fsci])));

               // TA is transmitted, if bit 4 is set to 1
               if (t0 & 0x10)
               {
                  t0f->prependChild(buildFieldInfo("[...1....] TA transmitted"));

                  int ta = frame[offset++];

                  if (ProtocolFrame *taf = ats->appendChild(buildFieldInfo("TA", QString("%1 [%2]").arg(ta, 2, 16, QChar('0')).arg(ta, 8, 2, QChar('0')))))
                  {
                     if (ta & 0x80)
                        taf->appendChild(buildFieldInfo(QString("[1.......] only support same rate for both directions")));
                     else
                        taf->appendChild(buildFieldInfo(QString("[0.......] supported different rates for each direction")));

                     if (ta & 0x40)
                        taf->appendChild(buildFieldInfo(QString("[.1......] supported 848 kbps PICC to PCD")));

                     if (ta & 0x20)
                        taf->appendChild(buildFieldInfo(QString("[..1.....] supported 424 kbps PICC to PCD")));

                     if (ta & 0x10)
                        taf->appendChild(buildFieldInfo(QString("[...1....] supported 212 kbps PICC to PCD")));

                     if (ta & 0x04)
                        taf->appendChild(buildFieldInfo(QString("[.....1..] supported 848 kbps PCD to PICC")));

                     if (ta & 0x02)
                        taf->appendChild(buildFieldInfo(QString("[......1.] supported 424 kbps PCD to PICC")));

                     if (ta & 0x01)
                        taf->appendChild(buildFieldInfo(QString("[.......1] supported 212 kbps PCD to PICC")));

                     if ((ta & 0x7f) == 0x00)
                        taf->appendChild(buildFieldInfo(QString("[.0000000] only 106 kbps supported")));
                  }
               }

               // TB is transmitted, if bit 5 is set to 1
               if (t0 & 0x20)
               {
                  t0f->prependChild(buildFieldInfo("[..1.....] TB transmitted"));

                  int tb = frame[offset++];

                  if (ProtocolFrame *tbf = ats->appendChild(buildFieldInfo("TB", QString("%1 [%2]").arg(tb, 2, 16, QChar('0')).arg(tb, 8, 2, QChar('0')))))
                  {
                     int sfgi = (tb & 0x0f);
                     int fwi = (tb >> 4) & 0x0f;

                     float sfgt = NFCA_SFGT[sfgi] * 1000;
                     float fwt = NFCA_FWT[fwi] * 1000;

                     tbf->appendChild(buildFieldInfo(QString("[%1....] frame waiting time FWT = %2 ms").arg(fwi, 4, 2, QChar('0')).arg(fwt, 0, 'f', 2)));
                     tbf->appendChild(buildFieldInfo(QString("[....%1] start-up frame guard time SFGT = %2 ms").arg(sfgi, 4, 2, QChar('0')).arg(sfgt, 0, 'f', 2)));
                  }
               }

               // TC is transmitted, if bit 5 is set to 1
               if (t0 & 0x40)
               {
                  t0f->prependChild(buildFieldInfo("[.1......] TC transmitted"));

                  int tc = frame[offset++];

                  if (ProtocolFrame *tcf = ats->appendChild(buildFieldInfo("TC", QString("%1 [%2]").arg(tc, 2, 16, QChar('0')).arg(tc, 8, 2, QChar('0')))))
                  {
                     if (tc & 0x01)
                        tcf->appendChild(buildFieldInfo("[.......1] NAD supported"));

                     if (tc & 0x02)
                        tcf->appendChild(buildFieldInfo("[......1.] CID supported"));
                  }
               }
            }

            if (offset < tl)
            {
               ats->appendChild(buildFieldInfo("HIST", toByteArray(frame, offset, tl - offset)));
            }
         }
      }

      root->appendChild(buildFieldInfo("CRC", toByteArray(frame, -2)));

      return root;
   }

   inline ProtocolFrame *parseResponseHLTA(const nfc::NfcFrame &frame)
   {
      int flags = frame.hasParityError() ? ProtocolFrame::Flags::ParityError : 0;

      return buildFrameInfo(frame.frameRate(), toByteArray(frame), frame.timeStart(), frame.timeEnd(), flags, ProtocolFrame::SenseFrame);
   }

   inline ProtocolFrame *parseResponsePPSr(const nfc::NfcFrame &frame)
   {
      int flags = frame.hasParityError() ? ProtocolFrame::Flags::ParityError : 0;

      return buildFrameInfo(frame.frameRate(), toByteArray(frame), frame.timeStart(), frame.timeEnd(), flags, ProtocolFrame::SelectionFrame);
   }

   inline ProtocolFrame *parseResponseAUTH(const nfc::NfcFrame &frame)
   {
      int flags = frame.hasParityError() ? ProtocolFrame::Flags::ParityError : 0;

      return buildFrameInfo(frame.frameRate(), toByteArray(frame), frame.timeStart(), frame.timeEnd(), flags, ProtocolFrame::AuthFrame);
   }

   inline ProtocolFrame *parseResponseIBlock(const nfc::NfcFrame &frame)
   {
      int pcb = frame[0], offset = 1;
      int flags = 0;

      flags |= frame.hasCrcError() ? ProtocolFrame::Flags::CrcError : 0;
      flags |= frame.hasParityError() ? ProtocolFrame::Flags::ParityError : 0;

      ProtocolFrame *root = buildFrameInfo(frame.frameRate(), toByteArray(frame), frame.timeStart(), frame.timeEnd(), flags, ProtocolFrame::InformationFrame);

      if (pcb & 0x08)
         root->appendChild(buildFieldInfo("CID", frame[offset++] & 0x0F));

      if (pcb & 0x04)
         root->appendChild(buildFieldInfo("NAD", frame[offset++] & 0xFF));

      if (offset < frame.limit() - 2)
      {
         if (flags & IS_APDU)
         {
            if (offset < frame.limit() - 4)
            {
               QByteArray data = toByteArray(frame, offset, frame.limit() - offset - 4);

               if (ProtocolFrame *payload = root->appendChild(buildFieldInfo("DATA", data)))
               {
                  payload->appendChild(buildFieldInfo(toString(data)));
               }
            }

            root->appendChild(buildFieldInfo("SW", toByteArray(frame, -4, 2)));
         }
         else
         {
            QByteArray data = toByteArray(frame, offset, frame.limit() - offset - 2);

            if (ProtocolFrame *payload = root->appendChild(buildFieldInfo("DATA", data)))
            {
               payload->appendChild(buildFieldInfo(toString(data)));
            }
         }
      }

      root->appendChild(buildFieldInfo("CRC", toByteArray(frame, -2)));

      return root;
   }

   inline ProtocolFrame *parseResponseRBlock(const nfc::NfcFrame &frame)
   {
      int flags = frame.hasParityError() ? ProtocolFrame::Flags::ParityError : 0;

      return buildFrameInfo(frame.frameRate(), toByteArray(frame), frame.timeStart(), frame.timeEnd(), flags, ProtocolFrame::InformationFrame);
   }

   inline ProtocolFrame *parseResponseSBlock(const nfc::NfcFrame &frame)
   {
      int flags = frame.hasParityError() ? ProtocolFrame::Flags::ParityError : 0;

      return buildFrameInfo(frame.frameRate(), toByteArray(frame), frame.timeStart(), frame.timeEnd(), flags, ProtocolFrame::InformationFrame);
   }

   inline ProtocolFrame *parseResponseUnknown(const nfc::NfcFrame &frame)
   {
      int flags = frame.hasParityError() ? ProtocolFrame::Flags::ParityError : 0;

      return buildFrameInfo(frame.frameRate(), toByteArray(frame), frame.timeStart(), frame.timeEnd(), flags, 0);
   }
};

struct ParserNfcB : Parser
{
   // FSDI to FSD conversion (frame size)
   constexpr static const int NFCB_FDS[] = {16, 24, 32, 40, 48, 64, 96, 128, 256, 0, 0, 0, 0, 0, 0, 0};

   // Frame waiting time (FWT = (256 x 16 / fc) * 2 ^ FWI)
   constexpr static const float NFCB_FWT[] = {0.000302065, 0.00060413, 0.00120826, 0.002416519, 0.004833038, 0.009666077, 0.019332153, 0.038664307, 0.077328614, 0.154657227, 0.309314454, 0.618628909, 1.237257817, 2.474515634, 4.949031268, 9.898062537};

   // Number of Slots
   constexpr static const float NFCB_REQB_SLOTS[] = {1, 2, 4, 8, 16, 0, 0, 0};

   // TR0min
   constexpr static const float NFCB_TR0_MIN[] = {0, 48 / FS, 16 / FS, 0};

   // TR1min
   constexpr static const float NFCB_TR1_MIN[] = {0, 64 / FS, 16 / FS, 0};

   ProtocolFrame *parse(unsigned int frameCount, const nfc::NfcFrame &frame)
   {
      this->frameCount = frameCount;

      ProtocolFrame *info = nullptr;

      if (frame.isPollFrame())
      {
         int command = frame[0];

         // Request Command
         if (command == 0x05)
            info = parseRequestREQB(frame);

            // Attrib request
         else if (command == 0x1d)
            info = parseRequestATTRIB(frame);

            // Unknown frame...
         else
            info = parseRequestUnknown(frame);

         lastCommand = command;
      }
      else
      {
         int command = lastCommand;

         // Request Command
         if (command == 0x05)
            info = parseResponseREQB(frame);

            // Attrib response
         else if (command == 0x1d)
            info = parseResponseATTRIB(frame);

            // Unknown frame...
         else
            info = parseResponseUnknown(frame);
      }

      lastFrame = frame;

      return info;
   }

   ProtocolFrame *parseRequestREQB(const nfc::NfcFrame &frame)
   {
      int flags = 0;
      int apf = frame[0];
      int afi = frame[1];
      int param = frame[2];
      int nslot = param & 0x07;

      flags |= frame.hasCrcError() ? ProtocolFrame::Flags::CrcError : 0;

      ProtocolFrame *root = buildFrameInfo(param & 0x8 ? "WUPB" : "REQB", frame.frameRate(), toByteArray(frame), frame.timeStart(), frame.timeEnd(), flags, ProtocolFrame::SenseFrame);

      if (ProtocolFrame *afif = root->appendChild(buildFieldInfo("AFI", QString("%1").arg(afi, 2, 16, QChar('0')))))
      {
         if (afi == 0x00)
            afif->appendChild(buildFieldInfo("[00000000] All families and sub-families"));
         else if ((afi & 0x0f) == 0x00)
            afif->appendChild(buildFieldInfo(QString("[%10000] All sub-families of family %2").arg(afi >> 4, 4, 2, QChar('0')).arg(afi >> 4)));
         else if ((afi & 0xf0) == 0x00)
            afif->appendChild(buildFieldInfo(QString("[0000%1] Proprietary sub-family %2 only").arg((afi & 0xf), 4, 2, QChar('0')).arg(afi & 0xf)));
         else if ((afi & 0xf0) == 0x10)
            afif->appendChild(buildFieldInfo(QString("[0001%1] Transport sub-family %2").arg((afi & 0xf), 4, 2, QChar('0')).arg(afi & 0xf)));
         else if ((afi & 0xf0) == 0x20)
            afif->appendChild(buildFieldInfo(QString("[0010%1] Financial sub-family %2").arg((afi & 0xf), 4, 2, QChar('0')).arg(afi & 0xf)));
         else if ((afi & 0xf0) == 0x30)
            afif->appendChild(buildFieldInfo(QString("[0011%1] Identification sub-family %2").arg((afi & 0xf), 4, 2, QChar('0')).arg(afi & 0xf)));
         else if ((afi & 0xf0) == 0x40)
            afif->appendChild(buildFieldInfo(QString("[0100%1] Telecommunication sub-family %2").arg((afi & 0xf), 4, 2, QChar('0')).arg(afi & 0xf)));
         else if ((afi & 0xf0) == 0x50)
            afif->appendChild(buildFieldInfo(QString("[0101%1] Medical sub-family %2").arg((afi & 0xf), 4, 2, QChar('0')).arg(afi & 0xf)));
         else if ((afi & 0xf0) == 0x60)
            afif->appendChild(buildFieldInfo(QString("[0110%1] Multimedia sub-family %2").arg((afi & 0xf), 4, 2, QChar('0')).arg(afi & 0xf)));
         else if ((afi & 0xf0) == 0x70)
            afif->appendChild(buildFieldInfo(QString("[0111%1] Gaming sub-family %2").arg((afi & 0xf), 4, 2, QChar('0')).arg(afi & 0xf)));
         else if ((afi & 0xf0) == 0x80)
            afif->appendChild(buildFieldInfo(QString("[1000%1] Data Storage sub-family %2").arg((afi & 0xf), 4, 2, QChar('0')).arg(afi & 0xf)));
         else
            afif->appendChild(buildFieldInfo(QString("[%1] RFU %2").arg(afi, 8, 2, QChar('0')).arg(afi)));
      }

      if (ProtocolFrame *paramf = root->appendChild(buildFieldInfo("PARAM", QString("%1").arg(afi, 2, 16, QChar('0')))))
      {
         if (param & 0x8)
            paramf->appendChild(buildFieldInfo("[....1...] WUPB command"));
         else
            paramf->appendChild(buildFieldInfo("[....0...] REQB command"));

         paramf->appendChild(buildFieldInfo(QString("[.....%1] Number of slots: %2").arg(nslot, 3, 2, QChar('0')).arg(NFCB_REQB_SLOTS[nslot])));
      }

      root->appendChild(buildFieldInfo("CRC", toByteArray(frame, -2)));

      return root;
   }

   ProtocolFrame *parseRequestATTRIB(const nfc::NfcFrame &frame)
   {
      int flags = 0;
      int param1 = frame[5];
      int param2 = frame[6];
      int param3 = frame[7];
      int param4 = frame[8];

      flags |= frame.hasCrcError() ? ProtocolFrame::Flags::CrcError : 0;

      ProtocolFrame *root = buildFrameInfo("ATTRIB", frame.frameRate(), toByteArray(frame), frame.timeStart(), frame.timeEnd(), flags, ProtocolFrame::SenseFrame);

      root->appendChild(buildFieldInfo("ID", toByteArray(frame, 1, 4)));

      if (ProtocolFrame *param1f = root->appendChild(buildFieldInfo("PARAM1", QString("%1 [%2]").arg(param1, 2, 16, QChar('0')).arg(param1, 8, 2, QChar('0')))))
      {
         int tr0min = (param1 >> 6) & 0x3;
         int tr1min = (param1 >> 4) & 0x3;

         if (tr0min)
            param1f->appendChild(buildFieldInfo(QString("[%1.....] Minimum TR0, %2 us").arg(tr0min, 2, 2, QChar('0')).arg(NFCB_TR0_MIN[tr0min] * 1E6, 0, 'f', 2)));
         else
            param1f->appendChild(buildFieldInfo(QString("[%1.....] Minimum TR0, DEFAULT").arg(tr0min, 2, 2, QChar('0'))));

         if (tr1min)
            param1f->appendChild(buildFieldInfo(QString("[%1.....] Minimum TR0, %2 us").arg(tr1min, 2, 2, QChar('0')).arg(NFCB_TR1_MIN[tr1min] * 1E6, 0, 'f', 2)));
         else
            param1f->appendChild(buildFieldInfo(QString("[%1.....] Minimum TR0, DEFAULT").arg(tr1min, 2, 2, QChar('0'))));

         if (param1 & 0x08)
            param1f->appendChild(buildFieldInfo(QString("[....%1..] EOF required: No").arg(param1 & 0x08 >> 2, 1, 2, QChar('0')).arg(param1 & 0x08 ? "No" : "Yes")));
         else
            param1f->appendChild(buildFieldInfo(QString("[....%1..] EOF required: Yes").arg(param1 & 0x08 >> 2, 1, 2, QChar('0')).arg(param1 & 0x08 ? "No" : "Yes")));

         if (param1 & 0x04)
            param1f->appendChild(buildFieldInfo(QString("[....%1..] SOF required: No").arg(param1 & 0x04 >> 3, 1, 2, QChar('0')).arg(param1 & 0x04 ? "No" : "Yes")));
         else
            param1f->appendChild(buildFieldInfo(QString("[....%1..] SOF required: Yes").arg(param1 & 0x08 >> 2, 1, 2, QChar('0')).arg(param1 & 0x08 ? "No" : "Yes")));
      }

      if (ProtocolFrame *param2f = root->appendChild(buildFieldInfo("PARAM2", QString("%1 [%2]").arg(param2, 2, 16, QChar('0')).arg(param2, 8, 2, QChar('0')))))
      {
         int fdsi = param2 & 0x0f;
         int fds = NFCB_FDS[fdsi];

         if ((param2 & 0xC0) == 0x00)
            param2f->appendChild(buildFieldInfo("[00......] selected 106 kbps PICC to PCD rate"));
         else if ((param2 & 0xC0) == 0x40)
            param2f->appendChild(buildFieldInfo("[01......] selected 212 kbps PICC to PCD rate"));
         else if ((param2 & 0xC0) == 0x80)
            param2f->appendChild(buildFieldInfo("[10......] selected 424 kbps PICC to PCD rate"));
         else if ((param2 & 0xC0) == 0xC0)
            param2f->appendChild(buildFieldInfo("[11......] selected 848 kbps PICC to PCD rate"));

         if ((param2 & 0x30) == 0x00)
            param2f->appendChild(buildFieldInfo("[..00....] selected 106 kbps PCD to PICC rate"));
         else if ((param2 & 0x30) == 0x10)
            param2f->appendChild(buildFieldInfo("[..01....] selected 212 kbps PCD to PICC rate"));
         else if ((param2 & 0x30) == 0x20)
            param2f->appendChild(buildFieldInfo("[..10....] selected 424 kbps PCD to PICC rate"));
         else if ((param2 & 0x30) == 0x30)
            param2f->appendChild(buildFieldInfo("[..11....] selected 848 kbps PCD to PICC rate"));

         param2f->appendChild(buildFieldInfo(QString("[....%1] maximum frame size, %2 bytes").arg(fdsi, 4, 2, QChar('0')).arg(fds)));
      }

      if (ProtocolFrame *param3f = root->appendChild(buildFieldInfo("PARAM3", QString("%1 [%2]").arg(param3, 2, 16, QChar('0')).arg(param3, 8, 2, QChar('0')))))
      {

      }

      if (ProtocolFrame *param4f = root->appendChild(buildFieldInfo("PARAM4", QString("%1 [%2]").arg(param4, 2, 16, QChar('0')).arg(param4, 8, 2, QChar('0')))))
      {

      }

      root->appendChild(buildFieldInfo("CRC", toByteArray(frame, -2)));

      return root;
   }

   ProtocolFrame *parseRequestUnknown(const nfc::NfcFrame &frame)
   {
      int flags = frame.hasCrcError() ? ProtocolFrame::Flags::CrcError : 0;

      ProtocolFrame *root = buildFrameInfo("(unk)", frame.frameRate(), toByteArray(frame), frame.timeStart(), frame.timeEnd(), flags, 0);

      root->appendChild(buildFieldInfo("CRC", toByteArray(frame, -2)));

      return root;
   }

   ProtocolFrame *parseResponseREQB(const nfc::NfcFrame &frame)
   {
      int rate = frame[9];
      int fdsi = (frame[10] >> 4) & 0x0f;
      int type = frame[10] & 0x0f;
      int fwi = (frame[11] >> 4) & 0x0f;
      int adc = (frame[11] >> 2) & 0x03;
      int fo = frame[11] & 0x3;
      int fds = NFCB_FDS[fdsi];
      float fwt = NFCB_FWT[fwi] * 1000;

      int flags = frame.hasCrcError() ? ProtocolFrame::Flags::CrcError : 0;

      ProtocolFrame *root = buildFrameInfo(frame.frameRate(), toByteArray(frame), frame.timeStart(), frame.timeEnd(), flags, ProtocolFrame::SenseFrame);

      root->appendChild(buildFieldInfo("PUPI", toByteArray(frame, 1, 4)));
      root->appendChild(buildFieldInfo("APP", toByteArray(frame, 5, 4)));

      if (ProtocolFrame *inff = root->appendChild(buildFieldInfo("PROTO", toByteArray(frame, 9, 3))))
      {
         // protocol rate
         if (ProtocolFrame *ratef = inff->appendChild(buildFieldInfo("RATE", QString("%1 [%2]").arg(frame[9], 2, 16, QChar('0')).arg(frame[9], 8, 2, QChar('0')))))
         {
            if (rate & 0x80)
               ratef->appendChild(buildFieldInfo(QString("[1.......] only support same rate for both directions")));
            else
               ratef->appendChild(buildFieldInfo(QString("[0.......] supported different rates for each direction")));

            if (rate & 0x40)
               ratef->appendChild(buildFieldInfo(QString("[.1......] supported 848 kbps PICC to PCD")));

            if (rate & 0x20)
               ratef->appendChild(buildFieldInfo(QString("[..1.....] supported 424 kbps PICC to PCD")));

            if (rate & 0x10)
               ratef->appendChild(buildFieldInfo(QString("[...1....] supported 212 kbps PICC to PCD")));

            if (rate & 0x04)
               ratef->appendChild(buildFieldInfo(QString("[.....1..] supported 848 kbps PCD to PICC")));

            if (rate & 0x02)
               ratef->appendChild(buildFieldInfo(QString("[......1.] supported 424 kbps PCD to PICC")));

            if (rate & 0x01)
               ratef->appendChild(buildFieldInfo(QString("[.......1] supported 212 kbps PCD to PICC")));

            if ((rate & 0x7f) == 0x00)
               ratef->appendChild(buildFieldInfo(QString("[.0000000] only 106 kbps supported")));
         }

         // frame size
         if (ProtocolFrame *protof = inff->appendChild(buildFieldInfo("FRAME", QString("%1 [%2]").arg(frame[10], 2, 16, QChar('0')).arg(frame[10], 8, 2, QChar('0')))))
         {
            protof->appendChild(buildFieldInfo(QString("[%1....] maximum frame size, %2 bytes").arg(fdsi, 4, 2, QChar('0')).arg(fds)));

            if (type == 0)
               protof->appendChild(buildFieldInfo("[....0000] PICC not compliant with ISO/IEC 14443-4"));
            else if (type == 1)
               protof->appendChild(buildFieldInfo("[....0001] PICC compliant with ISO/IEC 14443-4"));
            else
               protof->appendChild(buildFieldInfo(QString("[....%1] protocol type %2").arg(type, 4, 2, QChar('0')).arg(type)));
         }

         // other parameters
         if (ProtocolFrame *otherf = inff->appendChild(buildFieldInfo("OTHER", QString("%1 [%2]").arg(frame[11], 2, 16, QChar('0')).arg(frame[11], 8, 2, QChar('0')))))
         {
            otherf->appendChild(buildFieldInfo(QString("[%1....] frame waiting time FWT = %2 ms").arg(fwi, 4, 2, QChar('0')).arg(fwt, 0, 'f', 2)));

            if (adc == 0)
               otherf->appendChild(buildFieldInfo("[....00..] application is proprietary"));
            else if (adc == 1)
               otherf->appendChild(buildFieldInfo("[....01..] application is coded in APP field"));
            else
               otherf->appendChild(buildFieldInfo(QString("[....%1..] RFU").arg(adc, 2, 2, QChar('0'))));

            if (fo & 0x2)
               otherf->appendChild(buildFieldInfo("[......1.] NAD supported by the PICC"));

            if (fo & 0x1)
               otherf->appendChild(buildFieldInfo("[.......1] CID supported by the PICC"));
         }
      }

      // frame CRC
      root->appendChild(buildFieldInfo("CRC", toByteArray(frame, -2)));

      return root;
   }

   ProtocolFrame *parseResponseATTRIB(const nfc::NfcFrame &frame)
   {
      int flags = frame.hasCrcError() ? ProtocolFrame::Flags::CrcError : 0;

      ProtocolFrame *root = buildFrameInfo(frame.frameRate(), toByteArray(frame), frame.timeStart(), frame.timeEnd(), flags, ProtocolFrame::SenseFrame);

      root->appendChild(buildFieldInfo("CRC", toByteArray(frame, -2)));

      return root;
   }

   ProtocolFrame *parseResponseUnknown(const nfc::NfcFrame &frame)
   {
      int flags = frame.hasParityError() ? ProtocolFrame::Flags::ParityError : 0;

      return buildFrameInfo(frame.frameRate(), toByteArray(frame), frame.timeStart(), frame.timeEnd(), flags, 0);
   }
};

struct ProtocolParser::Impl
{
   ParserNfcA nfca;

   ParserNfcB nfcb;

   unsigned int frameCount;

   nfc::NfcFrame lastFrame;

   Impl() : frameCount(1)
   {
   }

   void reset()
   {
      frameCount = 1;

      nfca.reset();

      nfcb.reset();
   }

   ProtocolFrame *parse(const nfc::NfcFrame &frame)
   {
      ProtocolFrame *info = nullptr;

      if (frame.isNfcA())
      {
         info = nfca.parse(frameCount++, frame);
      }
      else if (frame.isNfcB())
      {
         info = nfcb.parse(frameCount++, frame);
      }

      return info;
   }

};

ProtocolParser::ProtocolParser(QObject *parent) : QObject(parent), impl(new Impl)
{
}

ProtocolParser::~ProtocolParser()
{
}

void ProtocolParser::reset()
{
   impl->reset();
}

ProtocolFrame *ProtocolParser::parse(const nfc::NfcFrame &frame)
{
   return impl->parse(frame);
}

