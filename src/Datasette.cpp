//#define HARNESS 1
#if not defined(TAPESUPPORT)
#define TAPESUPPORT 1
#endif
#if defined(TAPESUPPORT)

//I accidentally created a cycle based bookmark format before finding out that IDX files were a thing..
//Keeping the code incase it comes in handy
//#define PRECISIONBOOKMARKS 1

#if not defined(HARNESS)
#include "ff.h"
#else
#include "FileTranslationLayer.h"
#endif

#include "Datasette.h"
#include <stdlib.h>
#include <ctype.h>
#include <iterator>

extern "C"
{
#if not defined(HARNESS)
#include "rpiHardware.h"
#endif
}

namespace
{
	unsigned char BitWidth(unsigned long long x)
	{
		return x == 0 ? 1 : 64 - __builtin_clzll(x);
	}

	unsigned Sqrt(const unsigned n)
	{
		unsigned char shift = BitWidth(n);
		shift += shift & 1; // round up to next multiple of 2

		unsigned result = 0;

		do
		{
			shift -= 2;
			result <<= 1; // leftshift the result to make the next guess
			result |= 1;  // guess that the next bit is 1
			result ^= result * result > (n >> shift); // revert if guess too high
		} while (shift != 0);

		return result;
	}
}

namespace PiDevice
{
	template <typename TEnum>
	bool IsSet(TEnum a, TEnum  b)
	{
		return (static_cast<unsigned int>(a) & static_cast<unsigned int>(b)) != 0;
	}

	template <typename TEnum>
	void Set(TEnum a, bool state, TEnum& b)
	{
		b = static_cast<TEnum>(state
			? static_cast<unsigned int>(b) | static_cast<unsigned int>(a)
			: static_cast<unsigned int>(b) & ~static_cast<unsigned int>(a)
			);
	}

	Datasette::Datasette(ScreenLCD* screenLCD)
		: m_screenLCD(screenLCD)
		, m_tapeName(nullptr)
		, m_button(Button::None)
		, m_previousButton(Button::None)
		, m_tapePos(0)
		, m_cyclesLeft(1)
		, m_cyclesInStep(1)
		, m_halwayCycles(0)
		, m_tapVersion(0)
		, m_platform(Platform::C64)
		, m_videoStandard(VideoStandard::PAL)
		, m_hertzPerSecond(1000000)
		, m_tapeLength(0)
		, m_totalCyclesPosition(0)
		, m_tapeCyclesStartOffset(0)
		, m_tapeCounterOffset(0)
		, m_tape(nullptr)
		, m_signalsToC64(SignalsToC64::None)
		, m_signalsFromC64(SignalsFromC64::None)
		, m_selectedBookmark(-1)
		, m_noTapeConversonPoints(0)
		, m_tapeConversionPoints(nullptr)
		, m_tapeConvertBackPoints(nullptr)
	{
	}

	Datasette::~Datasette()
	{
		free(m_tapeConversionPoints);
		m_tapeConversionPoints = nullptr;
		free(m_tapeConvertBackPoints);
		m_tapeConvertBackPoints = nullptr;
		free(m_tapeName);
		m_tapeName = nullptr;
		free(m_tape);
		m_tape = nullptr;
	}

#define WHITE RGBA(0xff, 0xff, 0xff, 0xff)
#define BLACK RGBA(0x00, 0x00, 0x00, 0x00)

	void Datasette::DisplayMessage(const char* row1)
	{
		if (m_screenLCD)
		{
#if not defined(HARNESS)
			m_screenLCD->Clear(BLACK);
			m_screenLCD->PrintText(false, 0, 0, const_cast<char*>(row1), BLACK, WHITE);
			m_screenLCD->SwapBuffers();
#endif
		}
	}

	bool Datasette::IsTapeImageExtension(const char* tapeImageName)
	{
		return GetTapeImageTypeViaExtension(tapeImageName) != TapeType::Unknown;
	}

	Datasette::TapeType Datasette::GetTapeImageTypeViaExtension(const char* tapeImageName)
	{
		char* ext = strrchr((char*)tapeImageName, '.');

		if (ext)
		{
			if (toupper((char)ext[1]) == 'T' && toupper((char)ext[2]) == 'A' && toupper((char)ext[3]) == 'P')
				return TapeType::TAP;
		}
		return TapeType::Unknown;
	}

	Datasette::ErrorState Datasette::LoadTape(const char* tapName)
	{
		free(m_tapeName);
		DisplayMessage("Loading TAP");
		m_tapeName = static_cast<char*>(malloc(strlen(tapName)+1));
		strcpy(m_tapeName, tapName);

		char header[13];
		memset(header, 0, 13);

		FIL fp;
		FRESULT res = f_open(&fp, tapName, FA_READ);
		DisplayMessage("Loading TAP.");
		UINT br = 0;
		if (res != FR_OK)
			return OnFileError(fp, ErrorState::FileNotFound);

		DisplayMessage("Loading TAP..");
		res = f_read(&fp, &header[0], 12, &br);
		if (res != FR_OK)
			return OnFileError(fp, ErrorState::FileNotTAP);

		DisplayMessage("Loading TAP...");
		if (strncmp("C64-TAPE-RAW", header, 12) != 0)
			return OnFileError(fp, ErrorState::FileNotTAP);
		
		res = f_read(&fp, &m_tapVersion, 1, &br);
		if (res != FR_OK)
			return OnFileError(fp, ErrorState::Error);

		BYTE platform;
		res = f_read(&fp, &platform, 1, &br);
		if (res != FR_OK)
			return OnFileError(fp, ErrorState::Error);
		
		m_platform = static_cast<Platform>(platform);

		BYTE videoStandard;
		res = f_read(&fp, &videoStandard, 1, &br);
		if (res != FR_OK)
			return OnFileError(fp, ErrorState::Error);

		DisplayMessage("b");
		m_videoStandard = static_cast<VideoStandard>(videoStandard);

		BYTE reserved;
		res = f_read(&fp, &reserved, 1, &br);
		if (res != FR_OK)
			return OnFileError(fp, ErrorState::Error);

		unsigned char tapeLength[4];
		res = f_read(&fp, &tapeLength[0], 4, &br);
		if (res != FR_OK)
			return OnFileError(fp, ErrorState::Error);

		m_tapeLength = tapeLength[3] << 24 | tapeLength[2] << 16 | tapeLength[1] << 8 | tapeLength[0];

		DisplayMessage("Loading TAP.....");
		auto tapeBytes = static_cast<unsigned char*>(malloc(m_tapeLength+1));
		if (tapeBytes == nullptr)
			return OnFileError(fp, ErrorState::Error);

		DisplayMessage("Loading TAP.....");
		res = f_read(&fp, &tapeBytes[0], static_cast<UINT>(m_tapeLength), &br);
		if (res != FR_OK)
			return OnFileError(fp, ErrorState::TAPTooShort);

		DisplayMessage("Loading TAP......");
		m_noTapeConversonPoints = 0;
		long long realTapeLength = 0;
		for (int i = 0; i < m_tapeLength; ++i)
		{
			auto b = tapeBytes[i];
			if (b == 0 && m_tapVersion == 1)
			{
				i += 3;
				++m_noTapeConversonPoints;
			}
			++realTapeLength;
		}
		free(m_tape);
		free(m_tapeConversionPoints);
		free(m_tapeConvertBackPoints);

		//Since the tape is converted into an array of ints to allow rewinding and precalculating cycles
		//it no longer matches the original TAP size, which messes with the bookmarks if it doesn't track
		//where the file changes are.
		m_tapeConversionPoints = static_cast<unsigned long long*>(malloc(m_noTapeConversonPoints * sizeof(unsigned long long)));
		m_tapeConvertBackPoints = static_cast<unsigned long long*>(malloc(m_noTapeConversonPoints * sizeof(unsigned long long)));
		m_tape = static_cast<unsigned int*>(malloc((realTapeLength+1)*sizeof(unsigned int)));

		DisplayMessage("Loading TAP.......");
		
		auto tapeCount = 0;
		auto i = 0;
		int tapeConversionPointIndex = 0;		

		while(tapeCount < m_tapeLength)
		{
			int b = tapeBytes[tapeCount++];
			unsigned int cycles = 0;
			bool isConversionPoint = false;
			if (b == 0)
			{
				switch (m_tapVersion)
				{
				case 1:
					cycles = 0;
					m_tapeConversionPoints[tapeConversionPointIndex++] = static_cast<unsigned long long>(tapeCount)-1;
					for (int i = 0; i != 3; ++i)
					{
						b = tapeCount < m_tapeLength ? tapeBytes[tapeCount++] : 0;
						cycles += b << (i * 8);
					}
					isConversionPoint = true;
					break;
				default:
					cycles = 65535;
					break;
				}
			}
			else
			{
				cycles = 8 * b;
			}
			if (isConversionPoint)
			{
				if (tapeConversionPointIndex < m_noTapeConversonPoints)
					m_tapeConvertBackPoints[tapeConversionPointIndex] = i;
			}

			if(i < realTapeLength)
				m_tape[i] = ConvertCycleToOneMHz(cycles);
			++i;
		}

		free(tapeBytes);

		m_tapeLength = realTapeLength;

		auto err = HerzFromComputerAndVideo();
		if (err != ErrorState::None)
			return err;

		DisplayMessage("Loading TAP!......");
		LoadBookmarkFile();

		DisplayMessage("Loading TAP!!.....");
		return OnFileError(fp, ErrorState::None);
	}

	Datasette::ErrorState Datasette::SaveTape(const char* tapName)
	{
		//Not implemented. Does anyone really want this?
		return ErrorState::Error;
	}

	void Datasette::Reset()
	{
		m_tapePos = 0;
		m_cyclesLeft = 1;
		m_cyclesInStep = 1;
		m_totalCyclesPosition = 0;
		m_halwayCycles = 0;
		m_tapVersion = 1;
		m_platform = Platform::C64;
		m_videoStandard = VideoStandard::PAL;
		m_hertzPerSecond = 1000000;
		m_signalsToC64 = SignalsToC64::None;
		m_signalsFromC64 = SignalsFromC64::None;
	}

	long long Datasette::Update(const int cyclesPassed)
	{
		switch (m_button)
		{
		case Button::Play:
			Play(true, true, TapeDirection::Forward, -1, cyclesPassed);
			break;
		case Button::Pause:
			break;
		case Button::FastForward:
		{
			bool isOnTape = true;
			for (int i = 0; i != 8 && isOnTape; ++i)
				isOnTape = Play(false, false, TapeDirection::Forward, -1, 1);
		}
			break;
		case Button::Rewind:
		{
			bool isOnTape = true;
			for (int i = 0; i != 8 && isOnTape; ++i)
				isOnTape = Play(false, false, TapeDirection::Back, -1, 1);
		}
			break;
		case Button::Record:
			Record();
			break;
		case Button::Stop:
			m_signalsToC64 = SignalsToC64::None;
			return -1;
		default:
			Set(SignalsToC64::Sense, false, m_signalsToC64);
			break;
		}
		if (m_tapeLength < 1)
			return -1;

		return m_tapePos;
	}

	Datasette::SignalsToC64 Datasette::Signals(SignalsFromC64 signals)
	{
		m_signalsFromC64 = signals;
		return m_signalsToC64;
	}

	Datasette::ErrorState Datasette::HerzFromComputerAndVideo()
	{
		switch (m_platform)
		{
		default:
		case Platform::C64:
			switch (m_videoStandard)
			{
			case VideoStandard::PAL:
				m_hertzPerSecond = 985248;
				return ErrorState::None;
			case VideoStandard::NTSC:
			case VideoStandard::OLD_NTSC:
				m_hertzPerSecond = 1022730;
				return ErrorState::None;
			case VideoStandard::PALN:
				m_hertzPerSecond = 1023440;
				return ErrorState::None;
			default:
				break;
			}
			break;
		case Platform::VIC20:
			switch (m_videoStandard)
			{
			case VideoStandard::PAL:
				m_hertzPerSecond = 1108405;
				return ErrorState::None;
			case VideoStandard::NTSC:
				m_hertzPerSecond = 1022727;
				return ErrorState::None;
			case VideoStandard::OLD_NTSC:
				return ErrorState::OLDNTSC_NotImplemented;
			case VideoStandard::PALN:
				return ErrorState::PALN_NotImplemented;
			default:
				break;
			}
			break;
		case Platform::C16PLUS4:
			switch (m_videoStandard)
			{
			case VideoStandard::PAL:
				m_hertzPerSecond = 886724;
				return ErrorState::None;
			case VideoStandard::NTSC:
				m_hertzPerSecond = 894886;
				return ErrorState::None;
			case VideoStandard::OLD_NTSC:
				return ErrorState::OLDNTSC_NotImplemented;
			case VideoStandard::PALN:
				return ErrorState::PALN_NotImplemented;
			default:
				break;
			}
			break;
		case Platform::PET:
			switch (m_videoStandard)
			{
			case VideoStandard::PAL:
			case VideoStandard::NTSC:
				m_hertzPerSecond = 1000000;
				return ErrorState::None;
			case VideoStandard::OLD_NTSC:
				return ErrorState::OLDNTSC_NotImplemented;
			case VideoStandard::PALN:
				return ErrorState::PALN_NotImplemented;
			default:
				break;
			}
			break;
		case Platform::C5:
			switch (m_videoStandard)
			{
			case VideoStandard::PAL:
				m_hertzPerSecond = 985248;
				return ErrorState::None;
			case VideoStandard::NTSC:
				m_hertzPerSecond = 1022730;
				return ErrorState::None;
			case VideoStandard::OLD_NTSC:
				return ErrorState::OLDNTSC_NotImplemented;
			case VideoStandard::PALN:
				return ErrorState::PALN_NotImplemented;
			default:
				break;
			}
			break;
		case Platform::C6_7:
			switch (m_videoStandard)
			{
			case VideoStandard::PAL:
			case VideoStandard::NTSC:
				m_hertzPerSecond = 2000000;
				return ErrorState::None;
			case VideoStandard::OLD_NTSC:
				return ErrorState::OLDNTSC_NotImplemented;
			case VideoStandard::PALN:
				return ErrorState::PALN_NotImplemented;
			default:
				break;
			}
			break;
		}
		return ErrorState::Error;
	}
	
	int Datasette::ConvertCycleToOneMHz(const int cycles)
	{
		return (1000000 * cycles) / m_hertzPerSecond;
	}

	bool Datasette::Play(const bool listenToSignals, const bool setReadSignals, const Datasette::TapeDirection tapeDir, const long long stopAtCycles, const int cyclesPassed)
	{
		if (tapeDir == TapeDirection::None)
			return true;

		const int stepDir = tapeDir == TapeDirection::Forward ? 1 : -1;

		Set(SignalsToC64::Sense, true, m_signalsToC64);

		if (listenToSignals && !IsSet(m_signalsFromC64, SignalsFromC64::Motor))
			return true;

		m_cyclesLeft -= stepDir;
		
		m_totalCyclesPosition += stepDir;

		if (setReadSignals)
			Set(SignalsToC64::Read, m_cyclesLeft < m_halwayCycles, m_signalsToC64);

		if (m_totalCyclesPosition == stopAtCycles)
		{
			SetButton(Button::Stop);
			return false;
		}

		if ((m_cyclesLeft > 0 && stepDir > 0) || (m_cyclesLeft <= m_cyclesInStep && stepDir < 0))
			return true;
		
		if (setReadSignals)
			Set(SignalsToC64::Read, false, m_signalsToC64);

		if (m_tapePos >= m_tapeLength || m_tapePos < 0)
		{
			SetButton(Button::Stop);
			return false;
		}

		ObtainNewCycles(stepDir);
		return true;
	}

	void Datasette::ObtainNewCycles(const int stepDir)
	{
		auto b = m_tape[m_tapePos];
		m_tapePos += stepDir;

		m_cyclesLeft = b;
		m_cyclesInStep = m_cyclesLeft;
		if (stepDir < 0)
			m_cyclesLeft = 0;

		//About halfway the signal should go high
		m_halwayCycles = m_cyclesLeft / 2;
	}

	bool Datasette::Record()
	{
		Set(SignalsToC64::Sense, true, m_signalsToC64);

		if (!IsSet(m_signalsFromC64, SignalsFromC64::Motor))
			return true;

		return false;
	}

	void Datasette::PreviousButton()
	{
		m_button = m_previousButton;
	}

	Datasette::Button Datasette::ButtonState() const
	{
		return m_button;
	}

	void Datasette::SetButton(Button button)
	{
		m_previousButton = m_button;
		m_button = button;

		switch (m_button)
		{
		case Button::Play:
		case Button::FastForward:
			if (m_tapePos < 0)
			{
				m_tapePos = 0;				
			}
			ObtainNewCycles(1);
			break;
		case Button::Rewind:
			if (m_tapePos >= m_tapeLength)
			{
				m_tapePos = m_tapeLength - 1;
			}
			ObtainNewCycles(-1);
			break;
		case Button::Record:
			break;
		case Button::Stop:
			break;
		default:
			break;
		}

	}

	int Datasette::TapeCounterSimulation() const
	{
		return TapeCounterSimulationInternal(m_tapeCounterOffset);
	}

	int Datasette::TapeCounterSimulationInternal(const long long tapeCounterOffset) const
	{
		//Calculating the tape counter the same way that VICE does.
		static constexpr double pi = 3.1415926535;
		static constexpr double d = 1.27e-5;
		static constexpr double r = 1.07e-2;
		static constexpr double v = 4.76e-2;
		static constexpr double g = 0.525;
		static constexpr long long tapeCounterWrapAround = 1000;
		static constexpr long long datasetteCyclesPerSec = 1000000;
		static constexpr double dsC1 = v / d / pi;
		static constexpr double dsC2 = (r * r) / (d * d);
		static constexpr double dsC3 = r / d;

		return (tapeCounterWrapAround - tapeCounterOffset + (int)(g * (Sqrt(((m_totalCyclesPosition / datasetteCyclesPerSec) * dsC1) + dsC2) - dsC3))) % tapeCounterWrapAround;
	}

	int Datasette::MegaCyclesPassed() const
	{
		return static_cast<int>((m_totalCyclesPosition - m_tapeCyclesStartOffset) / 1000000);
	}

	const Datasette::TAPBookmarkFile& Datasette::Bookmarks() const
	{
		return m_bookmarkFile;
	}

	void Datasette::AddBookmark()
	{
#if defined(PRECISIONBOOKMARKS)
		AddBookmark(m_totalCyclesPosition, "Bookmark");
#else
		AddBookmark(m_tapePos, "Bookmark");
#endif
		m_selectedBookmark = m_bookmarkFile.BookmarkCount-1;
		SaveBookmarkFile();
	}

	void Datasette::AddBookmark(unsigned long long position, const char* name)
	{
		int bookmarkIndex = m_bookmarkFile.BookmarkCount;
		++m_bookmarkFile.BookmarkCount;
		if (m_bookmarkFile.BookmarkCount == 1)
			m_bookmarkFile.Bookmarks = static_cast<TAPBookmark**>(malloc(sizeof(TAPBookmark*)));
		else
		{
			auto newMem = static_cast<TAPBookmark**>(realloc(m_bookmarkFile.Bookmarks, m_bookmarkFile.BookmarkCount * sizeof(TAPBookmark*)));
			if (newMem == nullptr)
				return;

			m_bookmarkFile.Bookmarks = newMem;
		}
		if (m_bookmarkFile.Bookmarks != nullptr)
			m_bookmarkFile.Bookmarks[bookmarkIndex] = new TAPBookmark(position, name);
	}

	void Datasette::SelectBookmark(const int selectedBookmark)
	{
		m_selectedBookmark = selectedBookmark;
	}

	void Datasette::MoveTapeToSelectedBookmark()
	{
		if (m_selectedBookmark == -1 || m_selectedBookmark >= static_cast<int>(m_bookmarkFile.BookmarkCount))
			return;

		const TAPBookmark& bm = *m_bookmarkFile.Bookmarks[m_selectedBookmark];
		const long long pos = bm.Position();

#if defined(PRECISIONBOOKMARKS)
		const TapeDirection dir = pos >= m_totalCyclesPosition ? TapeDirection::Forward : TapeDirection::Back;
		if (m_tapePos < 0)
		{
			m_tapePos = 0;
			ObtainNewCycles(1);
		}
		else if (m_tapePos >= m_tapeLength)
		{
			m_tapePos = m_tapeLength - 1;
			ObtainNewCycles(-1);
		}

		while (Play(false, false, dir, pos, 1));
#else
		m_tapePos = pos;
		ObtainNewCycles(1);
		long long totalCyclesPosition = 0;
		for (int i = 0; i != pos; ++i)
			totalCyclesPosition += m_tape[i];

		m_totalCyclesPosition = totalCyclesPosition;
#endif
	}

	int Datasette::SelectedBookmarkIndex() const
	{
		return m_selectedBookmark;
	}

	void Datasette::RemoveSelectedBookmark()
	{
		if (m_selectedBookmark == -1 || m_bookmarkFile.Bookmarks == nullptr)
			return;

		free(m_bookmarkFile.Bookmarks[m_selectedBookmark]);
		for (int i = m_selectedBookmark; i != m_bookmarkFile.BookmarkCount - 1; ++i)
			m_bookmarkFile.Bookmarks[i] = m_bookmarkFile.Bookmarks[i + 1];

		--m_bookmarkFile.BookmarkCount;
		//Skipping realloc. Unlikely that this is going to be a memory hog.
		SaveBookmarkFile();
	}

	void Datasette::SetNewTapeStartPoint()
	{
		m_tapeCyclesStartOffset = m_totalCyclesPosition;
		m_tapeCounterOffset = TapeCounterSimulationInternal(0);
	}

	long long Datasette::TapeSize() const
	{
		return m_tapeLength;
	}

	Datasette::ErrorState Datasette::OnFileError(FIL& fp, ErrorState state)
	{
		f_close(&fp);
		return state;
	}

#if defined(PRECISIONBOOKMARKS)
	Datasette::ErrorState Datasette::LoadBookmarkFile()
	{
		m_bookmarkFile.BookmarkCount = 0;
		free(m_bookmarkFile.Bookmarks);

		char bookmarkFileName[256];
		strcpy(bookmarkFileName, m_tapeName);
		strcat(bookmarkFileName, ".bmk");
		
		FIL fp;
		FRESULT res = f_open(&fp, bookmarkFileName, FA_READ);
		UINT br = 0;
		if (res != FR_OK)
			return OnFileError(fp, ErrorState::FileNotFound);

		char positionStr[256];
		char nameStr[256];

		while (true)
		{
			char c = 0;
			int i = 0;
			while (c != ' ' && c != '\n')
			{
				res = f_read(&fp, &c, 1, &br);
				if (res != FR_OK || br == 0)
					return ErrorState::None;

				if (c != '\n' && c != '\r' && c != ' ')
					positionStr[i++] = c;
			}

			positionStr[i] = 0;

			if (c != ' ')
				break;
			
			i = 0;
			while (c != '\n')
			{
				res = f_read(&fp, &c, 1, &br);
				if (res != FR_OK || br == 0)
					break;

				if (c != '\n' && c != '\r')
					nameStr[i++] = c;
			}
			nameStr[i] = 0;

			AddBookmark(atol(positionStr), nameStr);
		}

		return ErrorState::None;
	}

	Datasette::ErrorState Datasette::SaveBookmarkFile()
	{
		char bookmarkFileName[256];
		strcpy(bookmarkFileName, m_tapeName);
		strcat(bookmarkFileName, ".bmk");

		FIL fp;
		FRESULT res = f_open(&fp, bookmarkFileName, FA_CREATE_ALWAYS | FA_WRITE);
		UINT bw = 0;
		if (res != FR_OK)
			return OnFileError(fp, ErrorState::FileNotCreated);

		char str[256];

		for (int i = 0; i != m_bookmarkFile.BookmarkCount; ++i)
		{
			auto& bm = *m_bookmarkFile.Bookmarks[i];
			memset(str, 0, 256);
			sprintf(str, "%lld", bm.Position());
			strcat(str, " ");
			strcat(str, bm.Name());
			strcat(str, "\n");

			res = f_write(&fp, str, static_cast<UINT>(strlen(str)), &bw);

			if (res != FR_OK)
				return OnFileError(fp, ErrorState::FileNotCreated);
		}

		f_close(&fp);
		return ErrorState::None;
	}

#else
	Datasette::ErrorState Datasette::LoadBookmarkFile()
	{
		m_bookmarkFile.BookmarkCount = 0;
		free(m_bookmarkFile.Bookmarks);

		char bookmarkFileName[256];
		strcpy(bookmarkFileName, m_tapeName);
		const auto filenameLen = strlen(bookmarkFileName);
		bookmarkFileName[filenameLen - 3] = 'I';
		bookmarkFileName[filenameLen - 2] = 'D';
		bookmarkFileName[filenameLen - 1] = 'X';

		FIL fp;
		FRESULT res = f_open(&fp, bookmarkFileName, FA_READ);
		UINT br = 0;
		if (res != FR_OK)
		{
			bookmarkFileName[filenameLen - 3] = 'i';
			bookmarkFileName[filenameLen - 2] = 'd';
			bookmarkFileName[filenameLen - 1] = 'x';
			res = f_open(&fp, bookmarkFileName, FA_READ);
			if (res != FR_OK)
				return OnFileError(fp, ErrorState::FileNotFound);
		}

		char positionStr[256];
		char nameStr[256];

		while (true)
		{
			char c = 0;
			int i = 0;
			bool rejectRestOfChars = false;
			while (c != ' ' && c != '\n')
			{
				res = f_read(&fp, &c, 1, &br);
				if (res != FR_OK || br == 0)
					return ErrorState::None;

				if (c == ';' || c == '#')
					rejectRestOfChars = true;

				if (c != '\n' && c != '\r' && c != ' ' && !rejectRestOfChars)
					positionStr[i++] = c;
			}

			positionStr[i] = 0;

			auto posStrLen = strlen(positionStr);

			if (posStrLen == 0 && !rejectRestOfChars)
				continue;
			else if (c != ' ' && c != '\t')
				break;

			while (c == ' ' || c == '\t')
			{
				res = f_read(&fp, &c, 1, &br);
				if (res != FR_OK || br == 0)
					return ErrorState::None;
			}

			i = 0;			
			do
			{
				if (c == ';' || c == '#')
					rejectRestOfChars = true;

				if (c != '\n' && c != '\r' && !rejectRestOfChars)
					nameStr[i++] = c;

				res = f_read(&fp, &c, 1, &br);
				if (res != FR_OK || br == 0)
					break;
			} while (c != '\n');

			nameStr[i] = 0;

			if (!rejectRestOfChars && posStrLen > 0 && strlen(nameStr) > 0)
				AddBookmark(ConvertCharIndexToIntIndex(strtoul(positionStr, nullptr, 0)), nameStr);
		}

		return ErrorState::None;
	}

	unsigned long long Datasette::ConvertCharIndexToIntIndex(unsigned long long charIndex)
	{
		if (m_noTapeConversonPoints == 0)
			return charIndex;

		//Every 3 byte cycles count is only a single index in the internal tape format
		//so a bookmark that is at index 24 in the originale file that has a 4 byte cycles at 10 and 13
		//actually resides at 24-3-3 in the internal structure.
		auto newIndex = charIndex;
		for (int i = 0; i != m_noTapeConversonPoints; ++i)
		{
			if (m_tapeConversionPoints[i] >= charIndex)
				break;

			newIndex -= 3;
		}
		return newIndex;
	}
	
	unsigned long long Datasette::ConvertIntIndexToCharIndex(unsigned long long intIndex)
	{
		if (m_noTapeConversonPoints == 0)
			return intIndex;

		//Every 3 byte cycles count is only a single index in the internal tape format
		auto newIndex = intIndex;
		for (int i = 0; i != m_noTapeConversonPoints; ++i)
		{
			if (m_tapeConvertBackPoints[i] >= intIndex)
				break;

			newIndex += 3;
		}
		return newIndex;
	}

	Datasette::ErrorState Datasette::SaveBookmarkFile()
	{
		char bookmarkFileName[256];
		strcpy(bookmarkFileName, m_tapeName);
		const auto filenameLen = strlen(bookmarkFileName);
		bookmarkFileName[filenameLen - 3] = 'i';
		bookmarkFileName[filenameLen - 2] = 'd';
		bookmarkFileName[filenameLen - 1] = 'x';

		FIL fp;
		FRESULT res = f_open(&fp, bookmarkFileName, FA_CREATE_ALWAYS | FA_WRITE);
		UINT bw = 0;
		if (res != FR_OK)
			return OnFileError(fp, ErrorState::FileNotCreated);

		char str[256];

		for (int i = 0; i != m_bookmarkFile.BookmarkCount; ++i)
		{
			auto& bm = *m_bookmarkFile.Bookmarks[i];
			auto convertedIndex = ConvertIntIndexToCharIndex(bm.Position());
			memset(str, 0, 256);
			sprintf(str, "0x%08llx %s\n", convertedIndex, bm.Name());

			res = f_write(&fp, str, static_cast<UINT>(strlen(str)), &bw);

			if (res != FR_OK)
				return OnFileError(fp, ErrorState::FileNotCreated);
		}

		f_close(&fp);
		return ErrorState::None;
	}

#endif

}

#endif