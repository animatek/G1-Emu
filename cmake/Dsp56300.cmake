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
