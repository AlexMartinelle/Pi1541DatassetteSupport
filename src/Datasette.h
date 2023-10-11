#pragma once
//#define HARNESS 1
#if not defined(TAPESUPPORT)
#define TAPESUPPORT 1
#endif

#if defined(TAPESUPPORT)
#if not defined(HARNESS)
#include "ff.h"
#else
#include "FileTranslationLayer.h"
#endif
#ifndef HARNESS
#include "ScreenLCD.h"
#endif
//using ScreenLCD = char;

#include <string.h>
#include <stdlib.h>

namespace PiDevice
{
	class Datasette
	{
	public:
		enum class TapeType
		{
			Unknown,
			TAP
		};
		enum class Platform
		{
			C64,
			VIC20,
			C16PLUS4,
			PET,
			C5,
			C6_7
		};
		enum class VideoStandard
		{
			PAL,
			NTSC,
			OLD_NTSC,
			PALN
		};
		enum class Button
		{
			None,
			Play,
			Pause,
			Stop,
			Record,
			FastForward,
			Rewind
		};
		enum class TapeDirection
		{
			None,
			Forward,
			Back
		};
		enum class ErrorState
		{
			None,
			FileNotFound,
			FileNotTAP,
			Error,
			TAPTooShort,
			OLDNTSC_NotImplemented,
			PALN_NotImplemented,
			BadBookmark,
			FileNotCreated
		};
		enum class SignalsToC64
		{
			None,
			Read = 1,
			Sense = 2
		};
		enum class SignalsFromC64
		{
			None,
			Write = 1,
			Motor = 2
		};
		
		class TAPBookmark
		{
		public:
			TAPBookmark()
				:m_position(0L)
				,m_name(nullptr)
			{
			}

			TAPBookmark(const long long position, const char* name)
				:m_position(position)				
				,m_name(static_cast<char*>(malloc(strlen(name))))
			{
				if (m_name != nullptr)
					strcpy(m_name, name);
			}

			virtual ~TAPBookmark()
			{
				free(m_name);
			}

			const unsigned long long Position() const { return m_position; }
			const char* Name() const { return m_name; }

			TAPBookmark& operator=(const TAPBookmark& bm)
			{
				m_position = bm.m_position;
				free(m_name);
				m_name = static_cast<char*>(malloc(strlen(bm.m_name)));
				if (m_name != nullptr)
					strcpy(m_name, bm.m_name);
				return *this;
			}

		private:
			unsigned long long m_position;
			char* m_name;
		};

		class TAPBookmarkFile
		{
		public:
			const int Version = 1;
			int BookmarkCount = 0;
			TAPBookmark** Bookmarks = nullptr;
			virtual ~TAPBookmarkFile()
			{
				for (int i = 0; i != BookmarkCount; ++i)
					free(Bookmarks[i]);

				free(Bookmarks);
			}
		};

		Datasette(ScreenLCD* screenLCD);
		virtual ~Datasette();

		ErrorState LoadTape(const char* tapName);
		ErrorState SaveTape(const char* tapName);
		void Reset();
		long long Update(const int cyclesPassed);
		SignalsToC64 Signals(SignalsFromC64 signals);
		void SetButton(Button button);
		Button ButtonState() const;
		void PreviousButton();
		int TapeCounterSimulation() const;
		int MegaCyclesPassed() const;
		const TAPBookmarkFile& Bookmarks() const;
		void AddBookmark();
		void SelectBookmark(const int selectedBookmark);
		void MoveTapeToSelectedBookmark();
		int SelectedBookmarkIndex() const;
		void RemoveSelectedBookmark();
		void SetNewTapeStartPoint();
		long long TapeSize() const;
		static TapeType GetTapeImageTypeViaExtension(const char* tapeImageName);
		static bool IsTapeImageExtension(const char* tapeImageName);
	private:
		bool Play(const bool listenToSignals, const bool setReadSignals, const TapeDirection tapeDir, const long long stopAtCycles, const int cyclesPassed);
		void ObtainNewCycles(const int stepDir);
		bool Record();
		int ConvertCycleToOneMHz(const int cycles);
		ErrorState HerzFromComputerAndVideo();
		ErrorState OnFileError(FIL& fp, ErrorState state);
		void AddBookmark(unsigned long long position, const char* name);
		ErrorState LoadBookmarkFile();
		ErrorState SaveBookmarkFile();
		void DisplayMessage(const char* row1);
		unsigned long long ConvertCharIndexToIntIndex(unsigned long long charIndex);
		unsigned long long ConvertIntIndexToCharIndex(unsigned long long charIndex);
		int TapeCounterSimulationInternal(const long long tapeCounterOffset) const;

		ScreenLCD* m_screenLCD;
		char* m_tapeName;
		TAPBookmarkFile m_bookmarkFile;
		Button m_button;
		Button m_previousButton;
		long long m_tapePos;
		int m_cyclesLeft;
		int m_cyclesInStep;
		int m_halwayCycles;
		BYTE m_tapVersion;
		Platform m_platform;
		VideoStandard m_videoStandard;
		int m_hertzPerSecond;
		long long m_tapeLength;
		long long m_totalCyclesPosition;
		long long m_tapeCyclesStartOffset;
		long long m_tapeCounterOffset;
		unsigned int* m_tape;
		SignalsToC64 m_signalsToC64;
		SignalsFromC64 m_signalsFromC64;
		int m_selectedBookmark;
		int m_noTapeConversonPoints;
		unsigned long long* m_tapeConversionPoints;
		unsigned long long* m_tapeConvertBackPoints;
	};
}
#endif
