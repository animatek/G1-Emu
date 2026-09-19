# Correcciones del nucleo necesarias para el G1, aplicadas solo a la copia de
# compilacion. El clon de Gearmulator sigue siendo la fuente y no se modifica.
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
		message(FATAL_ERROR "Revisar correccion G1 de ${name}: el nucleo externo ha cambiado")
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

# DO FOREVER conserva LC. En el final de bucle, FV evita decrementar LC o salir.
g1_dsp_replace(jitblock.cpp
	"\t\t\tm_asm.cmp(lc, asmjit::Imm(1));\n\t\t\tm_asm.jle(enddo);\n\t\t\tm_asm.dec(lc);"
	"\t\t\tconst auto repeatForever = m_asm.newLabel();\n\t\t\tm_asm.bitTest(sr, SRB_FV);\n\t\t\tm_asm.jnz(repeatForever);\n\t\t\tm_asm.cmp(lc, asmjit::Imm(1));\n\t\t\tm_asm.jle(enddo);\n\t\t\tm_asm.dec(lc);\n\t\t\tm_asm.bind(repeatForever);")

# Los DO anidados y ENDDO guardan/restauran tambien FV, no solo LF.
g1_dsp_replace(jitops.cpp
	"m_asm.or_(m_dspRegs.getSR(JitDspRegs::ReadWrite), asmjit::Imm(SR_LF));"
	"m_asm.and_(m_dspRegs.getSR(JitDspRegs::ReadWrite), asmjit::Imm(~SR_FV));\n\t\t\tm_asm.or_(m_dspRegs.getSR(JitDspRegs::ReadWrite), asmjit::Imm(SR_LF));")
g1_dsp_replace(jitops.cpp
	"m_asm.and_(r32(r), asmjit::Imm(SR_LF));"
	"m_asm.and_(r32(r), asmjit::Imm(SR_LF | SR_FV));")
g1_dsp_replace(jitops.cpp
	"m_asm.and_(r32(m_dspRegs.getSR(JitDspRegs::ReadWrite)), asmjit::Imm(~SR_LF));"
	"m_asm.and_(r32(m_dspRegs.getSR(JitDspRegs::ReadWrite)), asmjit::Imm(~(SR_LF | SR_FV)));")

# DMA con doble contador en origen y destino a la vez (DAM = 011 011 en el DMA0 del G1:
# copia X:$6C0 -> Y:bufer de salida en cada bloque). Gearmulator no lo implementa y en
# Release daba el bloque por hecho sin copiar: el audio no pasaba de un DSP al siguiente.
# Ambos lados comparten DCOH/DCOL y cada uno suma su DOR al terminar cada linea.
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

# Las transferencias de bloque (disparadas por DE) se hacen al momento. Retrasadas, el DMA0
# copiaba la entrada al bufer de salida despues de que las voces sumaran su muestra y la pisaba.
# En el DSP real van en paralelo y acaban mucho antes (unos 36 ciclos).
g1_dsp_replace(dma.cpp
	"constexpr bool g_delayedDmaTransfer = true;"
	"constexpr bool g_delayedDmaTransfer = false;")

# DMA por peticion en modo bloque sin borrar DE (DTM=100). El DSP 0 del G1 mete asi la entrada
# de audio L: el DMA3 lleva el RX del ESSI1 a X:$6C5 (bloque de 1 palabra) y su interrupcion
# (vector $1E) copia el RX del ESSI0 a X:$6C4. Gearmulator lo ignoraba en silencio (solo
# aceptaba palabra o linea). Una peticion mueve el bloque entero; con DE puesto, sigue armado.
g1_dsp_replace(dma.cpp
	"const auto isSupportedTransferMode = tm == TransferMode::WordTriggerRequest || tm == TransferMode::WordTriggerRequestClearDE || tm == TransferMode::LineTriggerRequestClearDE;"
	"const auto isSupportedTransferMode = tm == TransferMode::WordTriggerRequest || tm == TransferMode::WordTriggerRequestClearDE || tm == TransferMode::LineTriggerRequestClearDE || tm == TransferMode::BlockTriggerRequest || tm == TransferMode::BlockTriggerRequestClearDE;")
g1_dsp_replace(dma.cpp
	"		if(!bittest(m_dcr, De))\n			return;\n\n		if(execTransfer())\n			finishTransfer();"
	"		if(!bittest(m_dcr, De))\n			return;\n\n		const auto btm = getTransferMode();\n		if(btm == TransferMode::BlockTriggerRequest || btm == TransferMode::BlockTriggerRequestClearDE)\n		{\n			while(!execTransfer()) {}\n			finishTransfer();\n			return;\n		}\n\n		if(execTransfer())\n			finishTransfer();")

# DMA de direccion fija a direccion fija (DAM 100 100): un registro de periferico a una celda de
# memoria. El DSP 0 lo usa para las entradas de audio (RX del ESSI1 -> X:$6C5). No tenia rama:
# caia en el assert final y, en Release, daba el bloque por hecho sin copiar nada.
g1_dsp_replace(dma.cpp
	"		assert(false && \"DMA transfer mode not supported yet\");\n		return true;\n	}"
	"		if(agmS == AddressGenMode::SingleCounterAnoUpdate && agmD == AddressGenMode::SingleCounterAnoUpdate)\n		{\n			memWrite(areaD, m_ddr, memRead(areaS, m_dsr));\n			if(isRequestTrigger() && m_dco)\n			{\n				--m_dco;\n				return false;\n			}\n			m_dco = m_dcomInit;\n			return true;\n		}\n\n		assert(false && \"DMA transfer mode not supported yet\");\n		return true;\n	}")

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
