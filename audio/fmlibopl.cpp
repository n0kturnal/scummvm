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
	OPL2/3 interface implementation with drivers for
	- NokturnFM2 and NokturnFM3,
	- OPL2LPT and OPL3LPT,
	- USB OPL3 Express,
	- OPL2 Audio Board and OPL3 Duo!
	... more 
	(c) 2023-26 Paweł Góralski
 */

#define FORBIDDEN_SYMBOL_ALLOW_ALL
#include "common/scummsys.h"
#include "audio/fmopl.h"
#include "audio/fmlibopl.h"

#include "backends/platform/atari/atari-debug.h"

extern "C"
{
#include "platform/atari/hwinfo.h"	
#include "core/user_memory.h"
#include "oplShadowRegs.h"
}

#ifndef RELEASE_BUILD
#define FMLIB_ENABLE_LOGS 1
#endif

#define FMLIB_ENABLE_BUFFERED_OUTPUT false
#define FMLIB_ENABLE_CUSTOM_ALLOC 1

//static const char gsOpl3ExpressPortName[] = "";

#if FMLIB_ENABLE_CUSTOM_ALLOC
// custom allocators
#include "backends/platform/atari/dlmalloc.h"
#define FMLIB_MSPACE_SIZE 1*1024
#endif

namespace OPL
{
	namespace FMlibOPL
	{

#if FMLIB_ENABLE_CUSTOM_ALLOC

extern "C"
{
	static mspace s_mFMlibSpace = nullptr;
	static sMemoryCallbacks s_MemCallbacks; 
    static void *s_fmLibMemoryBase = nullptr;

	static void *fmlibAlloc(size_t amount, const eMemoryFlag flag, void* userData, const char* functionName, char* fileName, uint32_t lineNo)
	{
#if FMLIB_ENABLE_LOGS
		atari_debug("fmlibAlloc()");
#endif	
		return mspace_malloc(s_mFMlibSpace, amount);
	}

	static void* fmlibAlignedAlloc(size_t alignment, size_t amount, const eMemoryFlag flag, void* userData, const int8_t* functionName, int8_t* fileName, uint32_t lineNo)
	{
#if FMLIB_ENABLE_LOGS
		atari_debug("fmlibAlignedAlloc()");
#endif	
		return mspace_memalign(s_mFMlibSpace,alignment,amount);
	}

	static void* fmlibRealloc(void* pOriginal, size_t size, void* userData)
	{
#if FMLIB_ENABLE_LOGS
		atari_debug("fmlibRealloc()");
#endif	
		return mspace_realloc(s_mFMlibSpace,pOriginal,size);
	}

	static void fmlibFree(void* ptr, void* userData)
	{
#if FMLIB_ENABLE_LOGS
		atari_debug("fmlibFree()");
#endif	
		mspace_free(s_mFMlibSpace, ptr);
	}

	static void fmlibOutOfMemoryCb(void* userData)
	{	
#if FMLIB_ENABLE_LOGS
		atari_debug("FMLib out of memory!");
#endif	
	}

}
#endif		


#if FMLIB_ENABLE_LOGS
static const char *s_DebugConfigMsgStrs[FMlibOPL::dtNumDevices]=
{
	"Configuring NokturnFM2 cartridge",
	"Configuring NokturnFM3 cartridge",
	"Configuring RetroWave OPL3 Express",
	"Configuring Serdaco OPL2LPT",
	"Configuring Serdaco OPL3LPT",
	"Configuring CE OPL2 Audio Board",
	"Configuring CE OPL3 Duo!",
};
#endif

		OPL::OPL(Config::OplType type, FMlibOPL::OplDevice deviceType) : _type(type), _deviceType(deviceType), _activeReg(0), _initialized(false), _useBuffer(FMLIB_ENABLE_BUFFERED_OUTPUT), _incapableDevice(false) 
		{
#if FMLIB_ENABLE_LOGS
			atari_debug("FMlibOPL create");
#endif		

#if FMLIB_ENABLE_LOGS
			atari_debug("Requesting hardware info");
#endif		
 			Supexec(updateHardwareInfo);

			// defaults
			_ifaceCfg.deviceType = eFmDriverType::FMD_UNDEFINED;
			_ifaceCfg.soundchip = CM_UNDEFINED;
			_ifaceCfg.operationMode = CO_UNDEFINED;
			_ifaceCfg.setup = CC_UNDEFINED;

			_oplWrite = nullptr;
    		_oplEnqueWrite = nullptr;
    		_oplFlush = nullptr;
    		_oplReset = nullptr;

			if(_type == Config::kOpl2)
            {
            	_ifaceCfg.operationMode = CO_OPL2;	
            }

			if(_type == Config::kOpl3 || _type == Config::kDualOpl2)
            {
            	_ifaceCfg.operationMode = CO_OPL3;	
            }

			_ifaceCfg.setup = CC_SINGLE;

#if FMLIB_ENABLE_LOGS
			if(deviceType < dtNumDevices)
			{
				atari_debug(s_DebugConfigMsgStrs[deviceType]);
			}
#endif

			switch(deviceType)
			{
				case dtNokturnFM2:
				{
					_params.uParam.outputPort = OPT_ST_CART;
					_ifaceCfg.deviceType = eFmDriverType::FMD_OPLCART;
        			_ifaceCfg.soundchip = CM_OPL2;
        			_ifaceCfg.setup = CC_SINGLE;
        			
        			if(_type == Config::kOpl3 || _type == Config::kDualOpl2)
            		{
            			_incapableDevice = true;
            		}
				} break;
				case dtNokturnFM3:
				{
					_params.uParam.outputPort = OPT_ST_CART;
					_ifaceCfg.deviceType = eFmDriverType::FMD_OPLCART;
					_ifaceCfg.soundchip = CM_OPL3;
        			_ifaceCfg.setup = CC_SINGLE;
				} break;
				case dtRWOpl3Express:
				{
					_useBuffer = true; // use buffered output, sending data has signinficant overhead
					_params.uOpl3ExpressSettings.outputPort = OPT_USB;
					_ifaceCfg.deviceType = eFmDriverType::FMD_OPL3EXPRESS;
					_ifaceCfg.soundchip = CM_OPL3;
        			_ifaceCfg.setup = CC_SINGLE;
					
					//_params.uOpl3ExpressSettings.comPortName = gsOpl3ExpressPortName;
				} break;
				case dtOPL2LPT:
				{
					_params.uParam.outputPort = OPT_LPT;
					//_params.uOplxLptSettings.deviceName = NULL;
					_ifaceCfg.deviceType = eFmDriverType::FMD_OPL2LPT;
					_ifaceCfg.soundchip = CM_OPL2;
        			_ifaceCfg.setup = CC_SINGLE;
        			
        			if(_type == Config::kOpl3 || _type == Config::kDualOpl2)
            		{
            			_incapableDevice = true;
            		}
				} break;
				case dtOPL3LPT:
				{
					// OPL2 mode is forced internally on anything below TT due to lack of signals
					_params.uParam.outputPort = OPT_LPT;
					//_params.uOplxLptSettings.deviceName = NULL;
					_ifaceCfg.deviceType = eFmDriverType::FMD_OPL3LPT;
					_ifaceCfg.soundchip = CM_OPL3;
        			_ifaceCfg.setup = CC_SINGLE;
				} break;
				case dtOPL2AudioBoard:
				{
					_params.uParam.outputPort = OPT_LPT_SPI;
					_params.uCeAudioBoardSettings.isOpl2AudioBoard = true;
					_ifaceCfg.deviceType = eFmDriverType::FMD_CE_OPL2AUDIO_LPT_SPI;
					_ifaceCfg.soundchip = CM_OPL2;
        			_ifaceCfg.setup = CC_SINGLE;

        			if(_type == Config::kOpl3 || _type == Config::kDualOpl2)
            		{
            			_incapableDevice = true;
            		}
				} break;
				case dtOPL3Duo:
				{
					_params.uParam.outputPort = OPT_LPT_SPI;
					_params.uCeAudioBoardSettings.isOpl2AudioBoard = false;
					_ifaceCfg.deviceType = eFmDriverType::FMD_CE_OPL3DUO_LPT_SPI;
					_ifaceCfg.soundchip = CM_OPL3;
        			_ifaceCfg.setup = CC_SINGLE;
				} break;
				default:
				{
					warning("Unrecognized device type!");
				} break;
			};
		}
	
		OPL::~OPL()
		{
			if(_initialized == true)
			{
#if FMLIB_ENABLE_LOGS
				atari_debug("FMlibOPL destroy");
#endif	
				if (_useBuffer)
				{
					// flush
#if FMLIB_ENABLE_LOGS
					atari_debug("OPL flush");
#endif
            		_oplFlush();
				}

				_oplWrite = nullptr;
    			_oplEnqueWrite = nullptr;
    			_oplFlush = nullptr;
    			_oplReset = nullptr;
				
				(void)fmDeinit(&_iface);
				(void)fmDestroyInterface(&_iface);

				_incapableDevice = false;
				_useBuffer = false;
				_initialized = false;
#if FMLIB_ENABLE_CUSTOM_ALLOC		
				if(s_mFMlibSpace)
				{
					destroy_mspace(s_mFMlibSpace);
					Mfree(s_fmLibMemoryBase);
					s_fmLibMemoryBase = nullptr;
				}
#endif				
			}
		}
	
		bool OPL::init()
		{
#if FMLIB_ENABLE_LOGS
			atari_debug("FMlibOPL init");
#endif		
			if(_incapableDevice)
			{
#if FMLIB_ENABLE_LOGS
				atari_debug("FMlibOPL OPL2 device cannot emulate requested dual OPL2 / OPL3!");
#endif	
				return false;
			}

#if FMLIB_ENABLE_CUSTOM_ALLOC
			// set default callbacks
			setDefaultUserMemoryCallbacks();

			s_fmLibMemoryBase = (void *)Mxalloc((int32_t)FMLIB_MSPACE_SIZE + 256,(int16_t)3);	
			
			if (s_fmLibMemoryBase)
			{
				s_mFMlibSpace = create_mspace_with_base(s_fmLibMemoryBase, FMLIB_MSPACE_SIZE, 0);

				if(s_mFMlibSpace == 0)
				{
#if FMLIB_ENABLE_LOGS
					atari_debug("FMlibOPL create_mspace failed!");
#endif	
			 		return false;
				}

				// install custom memory allocator callbacks
				s_MemCallbacks.alloc = fmlibAlloc;
				s_MemCallbacks.alignedAlloc = fmlibAlignedAlloc;
				s_MemCallbacks.release = fmlibFree;
				s_MemCallbacks.realloc = fmlibRealloc;
				s_MemCallbacks.outOfMemory = fmlibOutOfMemoryCb;

				setUserMemoryCallbacks(&s_MemCallbacks);
			}
			else
			{
#if FMLIB_ENABLE_LOGS
				atari_debug("FMlibOPL Out of system memory!");
#endif			
				return false;
			}
#else
			setDefaultUserMemoryCallbacks();
#endif	
			_iface = fmCreateInterface(_ifaceCfg);
			
			if(_iface.setup != CC_UNDEFINED)
			{
				const int32_t retval = fmInit(&_iface, &_params);
	        
				if(retval >= 0)
				{
					_oplWrite = _iface.write;
    				_oplEnqueWrite = _iface.enqueWrite;
    				_oplFlush = _iface.flush;
    				_oplReset = _iface.reset;

					initDualOpl2OnOpl3(_type);
					_initialized = true;

#if FMLIB_ENABLE_LOGS
					atari_debug("FMlibOPL init OK");
#endif					
					return true;
				}
			}

#if FMLIB_ENABLE_LOGS
			atari_debug("FMlibOPL init failed!");
#endif			
			return false;			
		}
	
		void OPL::reset()
		{
#if FMLIB_ENABLE_LOGS
			atari_debug("FMlibOPL reset");
#endif
			for(int16_t i = 0; i < 256; i ++)
			{
				writeReg((int)i, 0);
			}
	
			if (_type == Config::kOpl3 || _type == Config::kDualOpl2)
			{
				for (int16_t i = 0; i < 256; i++)
				{
					writeReg((int)i + 256, 0);
				}
			}
			
			_activeReg = 0;

			initDualOpl2OnOpl3(_type);
		}

		void OPL::write(int portAddress, int value)
		{
			if (portAddress & 1)
			{
				writeReg(_activeReg, value);
				return;
			}
			else
			{
				if(_type == Config::kOpl2)
				{
					_activeReg = value & 0xff;
					return;
				}
				else
				{
					// opl3 / dual opl2
					_activeReg = (value & 0xff) | ((portAddress << 7) & 0x100);
					return;
				}

				warning("FMlibOPL: unsupported OPL mode %d", _type);
			}
		}

#if FMLIB_ENABLE_LOGS
static const char *s_DebugOplWriteStrs[FMlibOPL::dtNumDevices]=
{
	"FMlibOPL NokturnFM2 writeReg",
	"FMlibOPL NokturnFM3 writeReg",
	"FMlibOPL OPL3 Express writeReg",
	"FMlibOPL OPL2LPT writeReg",
	"FMlibOPL OPL3LPT writeReg",
	"FMlibOPL OPL2AudioBoard writeReg",
	"FMlibOPL OPL3Duo writeReg",
};
#endif

		void OPL::writeReg(int reg, int value)
		{
#if FMLIB_ENABLE_LOGS
			atari_debug(s_DebugOplWriteStrs[_deviceType]);
#endif

#if 0
			if (emulateDualOpl2OnOpl3(reg, value, _type))
			{
				sOplRegisterWrite regWrite;
			
				if ((reg & 0x100) && (_type != Config::kOpl2))
				{
					regWrite = {1, (uint8_t)reg, (uint8_t)value};
				}
				else
				{
					regWrite = {0, (uint8_t)reg, (uint8_t)value};
				}
				
				if (_useBuffer)
				{
					_oplEnqueWrite(&regWrite); 
				}
				else
				{
					_oplWrite(&regWrite);
				}
			}

#else
			if (_type == Config::kOpl3 || _type == Config::kDualOpl2)
			{
				reg &= 0x1ff;
			}
			else
			{
				reg &= 0xff;
			}
			
			value &= 0xff;

			if (emulateDualOpl2OnOpl3(reg, value, _type))
			{
				sOplRegisterWrite regWrite;

				if (reg < 0x100)
 				{
					regWrite = {0, (uint8_t)reg, (uint8_t)value};
				}
				else
				{
					regWrite = {1, (uint8_t)reg, (uint8_t)value};
				}
				
				//if (_useBuffer)
				//{
					_oplEnqueWrite(&regWrite); 
				//}
				//else
				//{
				//	_oplWrite(&regWrite);
				//}
			}
#endif
		}
	
		void OPL::onTimer()
		{
			//if (_useBuffer)
			{
				if (_initialized)
				{
					// flush
#if FMLIB_ENABLE_LOGS
					atari_debug("FMlibOPL flush");
#endif
            		_oplFlush();
				}
			}
	
			Audio::RealChip::onTimer();
		}

		OPL *create(Config::OplType type, OplDevice device)
		{
			return new OPL(type, device);
		}

	} // End of namespace fmlibopl3
} // End of namespace OPL
