/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

/*
	OPL interface for NokturnFM2, FMlibOPL carts, OPL2LPT, OPL3LPT, USB OPL3 Express and more through cross platform FMlib.
	(c) 2025-26 Paweł Góralski
 */

#ifndef AUDIO_HARDSYNTH_OPL_FMLIB_H
#define AUDIO_HARDSYNTH_OPL_FMLIB_H

#include "audio/fmopl.h"

#define PLATFORM_ATARI 1

#ifndef FMPL_INLINE
#define FMPL_INLINE inline
#endif

#ifndef restrict
#define restrict __restrict__
#endif

extern "C"
{
#include "fm/fm_defs.h"
#include "fmcore.h"
#include "fmutil.h"
}

namespace OPL
{
	namespace FMlibOPL
	{
		enum OplDevice : int16_t
		{
			dtNokturnFM2 = 0,
			dtNokturnFM3,
			dtRWOpl3Express,
			dtOPL2LPT,
			dtOPL3LPT,
			dtOPL2AudioBoard,
			dtOPL3Duo,
			dtNumDevices
		};

		class OPL : public ::OPL::OPL, public Audio::RealChip
		{
			private:
				Config::OplType _type;
				OplDevice _deviceType;
				sOplInterface _iface;
				funcPtrOplWrite _oplWrite;
    			funcPtrOplWrite _oplEnqueWrite;
    			funcPtrOplFlush _oplFlush;
    			funcPtrOplReset _oplReset;
				
				sInterfaceInitData _params;
				sOplInterfaceConfiguration _ifaceCfg;
				
				int _activeReg;
				bool _initialized;
				bool _useBuffer;
				bool _incapableDevice;
			public:
				explicit OPL(Config::OplType type, enum FMlibOPL::OplDevice deviceType);
				~OPL();
			
				bool init() override final;
				void reset() override final;
			
				void write(int portAddress, int value) override final;
				void writeReg(int reg, int value) override final;
			
			protected:
				
				void onTimer() override final;
		};
	
	} // End of namespace FMlibOPL
} // End of namespace OPL

#endif
