# Core fixes needed by the G1, applied only to the build copy. The Gearmulator clone
# remains the source and is never modified.
get_target_property(g1_dsp_source dsp56kEmu SOURCE_DIR)
set(g1_dsp_overlay "${CMAKE_BINARY_DIR}/g1-dsp/dsp56kEmu")
set(g1_dsp_prepare "${CMAKE_BINARY_DIR}/g1-dsp/prepare")
file(GLOB g1_dsp_files CONFIGURE_DEPENDS
	"${g1_dsp_source}/*.cpp" "${g1_dsp_source}/*.h" "${g1_dsp_source}/*.inl")
foreach(source IN LISTS g1_dsp_files)
	get_filename_component(name "${source}" NAME)
	configure_file("${source}" "${g1_dsp_prepare}/${name}" COPYONLY)
endforeach()

function(g1_dsp_replace name before after)
	set(path "${g1_dsp_prepare}/${name}")
	file(READ "${path}" contents)
	string(FIND "${contents}" "${before}" match)
	if(match EQUAL -1)
		message(FATAL_ERROR "Check the G1 fix for ${name}: the external core has changed")
	endif()
	string(REPLACE "${before}" "${after}" contents "${contents}")
	file(WRITE "${path}" "${contents}")
endfunction()

g1_dsp_replace(jitops.h
	"void op_Movem_aa(TWord op)\t\t\t\t{ errNotImplemented(op); }"
	"void op_Movem_aa(TWord op);")
g1_dsp_replace(jitops.h
	"void op_DoForever(TWord op)\t\t\t{ errNotImplemented(op); }"
	"void op_DoForever(TWord op);")
g1_dsp_replace(opcodeanalysis.h
	"case Movem_ea:\n\t\t\t{\n\t\t\t\tconst auto write = getFieldValue<Movem_ea, Field_W>(op);"
	"case Movem_aa:\n\t\t\treturn !getFieldValue<Movem_aa, Field_W>(op);\n\t\tcase Movem_ea:\n\t\t\t{\n\t\t\t\tconst auto write = getFieldValue<Movem_ea, Field_W>(op);")

# DO FOREVER keeps LC. At the loop end, FV prevents decrementing LC or leaving.
g1_dsp_replace(jitblock.cpp
	"\t\t\tm_asm.cmp(lc, asmjit::Imm(1));\n\t\t\tm_asm.jle(enddo);\n\t\t\tm_asm.dec(lc);"
	"\t\t\tconst auto repeatForever = m_asm.newLabel();\n\t\t\tm_asm.bitTest(sr, SRB_FV);\n\t\t\tm_asm.jnz(repeatForever);\n\t\t\tm_asm.cmp(lc, asmjit::Imm(1));\n\t\t\tm_asm.jle(enddo);\n\t\t\tm_asm.dec(lc);\n\t\t\tm_asm.bind(repeatForever);")

# One iteration per block (what the G1 uses) made the mask of that test zero. AArch64 cannot
# encode an immediate of zero for TST: asmjit refused the instruction, the rest of the block
# was never emitted and the DSP ran into it, which is the illegal instruction on Apple
# Silicon. The test is always true with that mask, so the jump is unconditional.
g1_dsp_replace(jitblock.cpp
	"\t\t\tif(m_config.maxDoIterations)\n\t\t\t{\n\t\t\t\tassert(asmjit::Support::isPowerOf2(m_config.maxDoIterations));\n\t\t\t\tm_asm.test_(_regLC, asmjit::Imm(m_config.maxDoIterations-1));\n\t\t\t\tm_asm.jz(skip);\n\t\t\t}"
	"\t\t\tif(m_config.maxDoIterations > 1)\n\t\t\t{\n\t\t\t\tassert(asmjit::Support::isPowerOf2(m_config.maxDoIterations));\n\t\t\t\tm_asm.test_(_regLC, asmjit::Imm(m_config.maxDoIterations-1));\n\t\t\t\tm_asm.jz(skip);\n\t\t\t}\n\t\t\telse if(m_config.maxDoIterations)\n\t\t\t{\n\t\t\t\tm_asm.jmp(skip);\n\t\t\t}")

# Nested DO and ENDDO also save/restore FV, not only LF. One form for both architectures: every
# immediate here encodes as an AArch64 logical immediate, checked by cross-assembling the
# sequences with asmjit's arm64 backend on an x86 host (see NOTES.md, "The DSP JIT on ARM").
g1_dsp_replace(jitops.cpp
	"m_asm.or_(m_dspRegs.getSR(JitDspRegs::ReadWrite), asmjit::Imm(SR_LF));"
	"m_asm.and_(m_dspRegs.getSR(JitDspRegs::ReadWrite), asmjit::Imm(~SR_FV));\n\t\t\tm_asm.or_(m_dspRegs.getSR(JitDspRegs::ReadWrite), asmjit::Imm(SR_LF));")
g1_dsp_replace(jitops.cpp
	"m_asm.and_(r32(r), asmjit::Imm(SR_LF));"
	"m_asm.and_(r32(r), asmjit::Imm(SR_LF | SR_FV));")
g1_dsp_replace(jitops.cpp
	"m_asm.and_(r32(m_dspRegs.getSR(JitDspRegs::ReadWrite)), asmjit::Imm(~SR_LF));"
	"m_asm.and_(r32(m_dspRegs.getSR(JitDspRegs::ReadWrite)), asmjit::Imm(~(SR_LF | SR_FV)));")

# DMA with dual counters on source and destination at once (DAM = 011 011 in the G1's DMA0:
# copies X:$6C0 -> Y:output buffer on every block). Gearmulator does not implement it and in
# Release it reported the block done without copying: audio never passed from one DSP to the next.
# Both sides share DCOH/DCOL and each adds its DOR at the end of every line.
g1_dsp_replace(dma.cpp
	[=[		assert(false && "DMA transfer mode not supported yet");]=]
	[=[		if(agmS <= AddressGenMode::DualCounterDOR3 && agmD <= AddressGenMode::DualCounterDOR3)
		{
			const auto isLineTransfer = getTransferMode() == TransferMode::LineTriggerRequestClearDE;
			const auto dorS = m_dma.getDOR(static_cast<TWord>(agmS));
			const auto dorD = m_dma.getDOR(static_cast<TWord>(agmD));

			do
			{
				memWrite(areaD, m_ddr, memRead(areaS, m_dsr));

				m_dsr = (m_dcol == 0 ? m_dsr + dorS : m_dsr + 1) & 0xffffff;

				if(dualModeIncrement(m_ddr, dorD))
					return true;
			}
			while(!isRequestTrigger() || (isLineTransfer && m_dcol != m_dcolInit));

			return false;
		}

		assert(false && "DMA transfer mode not supported yet");]=])

# Block transfers (triggered by DE) happen immediately. Delayed, DMA0 copied the input to the
# output buffer after the voices had added their sample, and overwrote it.
# On the real DSP they run in parallel and finish much earlier (about 36 cycles).
g1_dsp_replace(dma.cpp
	"constexpr bool g_delayedDmaTransfer = true;"
	"constexpr bool g_delayedDmaTransfer = false;")

# DMA per request in block mode without clearing DE (DTM=100). The G1's DSP 0 brings in the
# left audio input this way: DMA3 moves ESSI1 RX to X:$6C5 (a 1-word block) and its interrupt
# (vector $1E) copies ESSI0 RX to X:$6C4. Gearmulator silently ignored it (it only accepted
# word or line). One request moves the whole block; with DE set, it stays armed.
g1_dsp_replace(dma.cpp
	"const auto isSupportedTransferMode = tm == TransferMode::WordTriggerRequest || tm == TransferMode::WordTriggerRequestClearDE || tm == TransferMode::LineTriggerRequestClearDE;"
	"const auto isSupportedTransferMode = tm == TransferMode::WordTriggerRequest || tm == TransferMode::WordTriggerRequestClearDE || tm == TransferMode::LineTriggerRequestClearDE || tm == TransferMode::BlockTriggerRequest || tm == TransferMode::BlockTriggerRequestClearDE;")
g1_dsp_replace(dma.cpp
	"		if(!bittest(m_dcr, De))\n			return;\n\n		if(execTransfer())\n			finishTransfer();"
	"		if(!bittest(m_dcr, De))\n			return;\n\n		const auto btm = getTransferMode();\n		if(btm == TransferMode::BlockTriggerRequest || btm == TransferMode::BlockTriggerRequestClearDE)\n		{\n			while(!execTransfer()) {}\n			finishTransfer();\n			return;\n		}\n\n		if(execTransfer())\n			finishTransfer();")

# DMA from a fixed address to a fixed address (DAM 100 100): a peripheral register to a memory
# cell. The G1's DSP 0 uses it for the audio inputs (ESSI1 RX -> X:$6C5). It had no branch:
# it fell into the final assert and, in Release, reported the block done without copying.
g1_dsp_replace(dma.cpp
	"		assert(false && \"DMA transfer mode not supported yet\");\n		return true;\n	}"
	"		if(agmS == AddressGenMode::SingleCounterAnoUpdate && agmD == AddressGenMode::SingleCounterAnoUpdate)\n		{\n			memWrite(areaD, m_ddr, memRead(areaS, m_dsr));\n			if(isRequestTrigger() && m_dco)\n			{\n				--m_dco;\n				return false;\n			}\n			m_dco = m_dcomInit;\n			return true;\n		}\n\n		assert(false && \"DMA transfer mode not supported yet\");\n		return true;\n	}")

# CMPM compares magnitudes, and alu_cmp takes the absolute value of its operand in place. When the
# operand is an accumulator, decode_JJJ_read_56 hands over the JIT's cached register for it, not a
# copy, so the rest of the block saw |a| instead of a, and if a was already dirty (`sub x1,b a1,a`
# just before) |a| was even written back. It is what silenced every sawtooth (OscA/OscB wave 2,
# OscSlvC): the saw branch does `cmpm a,b` then `tgt a,b`. Blocks of one instruction hid it. A copy.
g1_dsp_replace(jitops_alu.cpp
	[=[		const auto r = decode_JJJ_read_56(JJJ, !D);
		alu_cmp(D, r64(r.get()), true);]=]
	[=[		const auto r = decode_JJJ_read_56(JJJ, !D);
		const RegGP v(m_block);
		m_asm.mov(v, r64(r.get()));	// alu_cmp takes the absolute value in place: never on a cached register
		alu_cmp(D, v, true);]=])

# GT and LE on x86-64 tested the parity of Z, N and V: right in six of the eight cases, wrong when
# Z = 1 and N != V, where GT came out true and LE false. The AArch64 version computes
# (N ^ V) | Z and is right. Same here, in a scratch register.
g1_dsp_replace(jitops_decode_x64.cpp
	[=[				// (SRB_Z + (SRB_N != SRB_V)) == 0
				ccrMaskTest(static_cast<CCRMask>(CCR_Z | CCR_N | CCR_V));
				return asmjit::x86::CondCode::kParityEven;]=]
	[=[				// (SRB_Z + (SRB_N != SRB_V)) == 0
				constexpr auto mask = static_cast<CCRMask>(CCR_Z | CCR_N | CCR_V);
				m_ccrRead |= mask;
				updateDirtyCCR(mask);
				const RegGP t(m_block);
				const RegGP z(m_block);
				const auto sr = m_dspRegs.getSR(JitDspRegs::Read).r32();
				m_asm.mov(t.get().r32(), sr);
				m_asm.shr(t.get().r32(), asmjit::Imm(CCRB_N - CCRB_V));	// N down to V's bit
				m_asm.xor_(t.get().r32(), sr);							// that bit is N ^ V
				m_asm.and_(t.get().r32(), asmjit::Imm(CCR_V));
				m_asm.mov(z.get().r32(), sr);
				m_asm.and_(z.get().r32(), asmjit::Imm(CCR_Z));
				m_asm.or_(t.get().r32(), z.get().r32());				// ZF: (N ^ V) | Z == 0
				return asmjit::x86::CondCode::kZero;]=])
g1_dsp_replace(jitops_decode_x64.cpp
	[=[				// (SRB_Z + (SRB_N != SRB_V)) == 1
				ccrMaskTest(static_cast<CCRMask>(CCR_Z | CCR_N | CCR_V));
				return asmjit::x86::CondCode::kParityOdd;]=]
	[=[				// (SRB_Z + (SRB_N != SRB_V)) == 1
				constexpr auto mask = static_cast<CCRMask>(CCR_Z | CCR_N | CCR_V);
				m_ccrRead |= mask;
				updateDirtyCCR(mask);
				const RegGP t(m_block);
				const RegGP z(m_block);
				const auto sr = m_dspRegs.getSR(JitDspRegs::Read).r32();
				m_asm.mov(t.get().r32(), sr);
				m_asm.shr(t.get().r32(), asmjit::Imm(CCRB_N - CCRB_V));
				m_asm.xor_(t.get().r32(), sr);
				m_asm.and_(t.get().r32(), asmjit::Imm(CCR_V));
				m_asm.mov(z.get().r32(), sr);
				m_asm.and_(z.get().r32(), asmjit::Imm(CCR_Z));
				m_asm.or_(t.get().r32(), z.get().r32());
				return asmjit::x86::CondCode::kNotZero;]=])

# An ESSI on the fine schedule (the links between DSPs) that sat idle owed the core every frame of
# that time. The catch-up loop counts from fineLastClock, which only moves when the port is served or
# its CRA is rewritten, so when a DSP with nothing to do (TX and RX off for seconds) woke up, it emitted
# them all at once: millions of frames, in one call, into a ring of 32768. The ring filled, the write
# callback waits for room, and the only thread that can make room is the one waiting: a deadlock, seen
# uploading nmedit's korg.pch (a 434-million-cycle gap on DSP 0). A real port does not queue the frames
# of the time it was off, so an anchor more than 64 periods behind is put back at 64.
g1_dsp_replace(esaiclock.cpp
	[=[while (static_cast<uint64_t>(ic - e.fineLastClock) >= e.finePeriod)]=]
	[=[const uint64_t g1MaxCatchUp = static_cast<uint64_t>(e.finePeriod) * 64u;
					if (static_cast<uint64_t>(ic - e.fineLastClock) > g1MaxCatchUp)
						e.fineLastClock = ic > g1MaxCatchUp ? ic - g1MaxCatchUp : 0;
					while (static_cast<uint64_t>(ic - e.fineLastClock) >= e.finePeriod)]=])

foreach(source IN LISTS g1_dsp_files)
	get_filename_component(name "${source}" NAME)
	configure_file("${g1_dsp_prepare}/${name}" "${g1_dsp_overlay}/${name}" COPYONLY)
endforeach()

get_target_property(g1_dsp_sources dsp56kEmu SOURCES)
set(g1_dsp_build_sources)
foreach(source IN LISTS g1_dsp_sources)
	get_filename_component(name "${source}" NAME)
	if(EXISTS "${g1_dsp_overlay}/${name}")
		list(APPEND g1_dsp_build_sources "${g1_dsp_overlay}/${name}")
	else()
		list(APPEND g1_dsp_build_sources "${source}")
	endif()
endforeach()
set_property(TARGET dsp56kEmu PROPERTY SOURCES "${g1_dsp_build_sources}")
target_include_directories(dsp56kEmu BEFORE PUBLIC "${CMAKE_BINARY_DIR}/g1-dsp")
target_include_directories(dsp56kEmu PRIVATE "${g1_dsp_source}")
target_sources(dsp56kEmu PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../g1Lib/dsp56300.cpp")
