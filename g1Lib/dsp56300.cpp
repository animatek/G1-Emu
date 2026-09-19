// dsp56300 JIT extensions needed by the G1 OS (GPLv3).
// cmake/Dsp56300.cmake declares these instructions in the build copy of the core;
// the external clone stays untouched.
#include "dsp56kEmu/jitops.h"
#include "dsp56kEmu/jitops_mem.inl"

namespace dsp56k
{
	void JitOps::op_Movem_aa(const TWord op)
	{
		const auto reg = getFieldValue<Movem_aa, Field_dddddd>(op);
		const auto address = getFieldValue<Movem_aa, Field_aaaaaa>(op);
		DspValue value(m_block, UsePooledTemp);
		if(getFieldValue<Movem_aa, Field_W>(op))
		{
			m_block.mem().readDspMemory(value, MemArea_P, address);
			decode_dddddd_write(reg, value);
		}
		else
		{
			// decode keeps the side effects: reading SSH pops a word off the
			// stack. The OS uses it to rewrite the IRQD target.
			decode_dddddd_read(value, reg);
			m_block.mem().writeDspMemory(MemArea_P, address, value);
			const DspValue target(m_block, address, DspValue::Immediate24);
			m_block.mem().mov(m_block.pMemWriteAddress(), target);
			m_block.mem().mov(m_block.pMemWriteValue(), value);
			m_resultFlags |= WritePMem;
		}
	}

	void JitOps::op_DoForever(const TWord)
	{
		// Like DO, it pushes LA/LC and PC/SR; unlike DO, it keeps LC
		// and sets FV to repeat even if the counter is zero.
		{
			DspValue la(m_block), lc(m_block);
			m_dspRegs.getLA(la);
			m_dspRegs.getLC(lc);
			setSSHSSL(la, lc);
		}
		m_asm.mov(m_dspRegs.getLA(JitDspRegs::Write), asmjit::Imm(getOpWordB()));
		pushPCSR();
		m_asm.or_(m_dspRegs.getSR(JitDspRegs::ReadWrite), asmjit::Imm(SR_LF | SR_FV));
		m_resultFlags |= WriteToLA;
	}
}
