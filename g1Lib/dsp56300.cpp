// Extensiones del JIT de dsp56300 necesarias para el OS del G1 (GPLv3).
// cmake/Dsp56300.cmake declara estas instrucciones en la copia de compilacion
// del nucleo; el clon externo se mantiene intacto.
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
			// decode conserva los efectos laterales: leer SSH saca una palabra
			// de la pila. El OS lo usa para reescribir el destino de IRQD.
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
		// Como DO, apila LA/LC y PC/SR; a diferencia de DO, conserva LC
		// y activa FV para repetir incluso si el contador vale cero.
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
