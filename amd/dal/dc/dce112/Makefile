#
# Makefile for the 'controller' sub-component of DAL.
# It provides the control and status of HW CRTC block.

DCE112 = dce112_link_encoder.o dce112_compressor.o dce112_hw_sequencer.o \
dce112_resource.o dce112_clock_source.o dce112_mem_input.o dce112_opp_formatter.o \
dce112_opp.o

AMD_DAL_DCE112 = $(addprefix $(AMDDALPATH)/dc/dce112/,$(DCE112))

AMD_DAL_FILES += $(AMD_DAL_DCE112)
