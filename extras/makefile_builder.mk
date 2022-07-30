.PRECIOUS: %/.

ASM_TO_COMPILE 	=		# define sources you wish
C_TO_COMPILE 	= 		# to build as these when
CPP_TO_COMPILE	= 		# calling the makefile_builder
EXE_TARGET		= 		# also define the EXE_TARGET


WRAPPER_SOURCES_C	=
WRAPPER_SOURCES_CPP =

ifeq ($(OS),Windows_NT)
# Windows + minGW
ECHO 			= echo -e
ASM_FLAGS 		= -fwin32
LINKER_FLAGS 	= -lpsapi -static-libgcc -static-libstdc++
else
# Linux
ECHO 			= echo
ASM_FLAGS 		= -felf
LINKER_FLAGS 	= -lX11 -lGL -lpthread -lpng -lstdc++fs
endif

# ANSI color palette
ANSI_RED 		:= "\033[1;31m"
ANSI_CYAN 		:= "\033[1;36m"
ANSI_YELLOW 	:= "\033[1;33m"
ANSI_GREEN 		:= "\033[1;32m"
ANSI_DEFAULT 	:= "\033[0m"

build_dir 		:= build
obj_dir			:= $(build_dir)/objs
dep_dir			:= $(build_dir)/deps
orig_o_dir 		:= $(obj_dir)/orig
orig_d_dir 		:= $(dep_dir)/orig

TARGET			:= $(build_dir)/$(EXE_TARGET)

all: $(TARGET)


clean:
	@$(ECHO) $(ANSI_RED) ------------- Cleaning -------------- $(ANSI_DEFAULT)
	@rm -rf $(build_dir)

%/.:
	@$(ECHO) $(ANSI_CYAN) ----------- Creating Directory : $(@D) ----------------- $(ANSI_DEFAULT)
	@mkdir -p $(@D)

ORIG_C_SOURCES		:= $(C_TO_COMPILE) $(WRAPPER_SOURCES_C)
ORIG_CPP_SOURCES	:= $(CPP_TO_COMPILE) $(WRAPPER_SOURCES_CPP)

ORIG_C_DEPS			:= $(ORIG_C_SOURCES:%.c=$(orig_d_dir)/c/%.d)
ORIG_C_OBJS			:= $(ORIG_C_SOURCES:%.c=$(orig_o_dir)/c/%.o)
ORIG_CPP_DEPS		:= $(ORIG_CPP_SOURCES:%.cpp=$(orig_d_dir)/cpp/%.d)
ORIG_CPP_OBJS		:= $(ORIG_CPP_SOURCES:%.cpp=$(orig_o_dir)/cpp/%.o)

ORIG_ASM_OBJS		:= $(ASM_TO_COMPILE:%.asm=$(orig_o_dir)/asm/%.o)

INCLUDE_DIRS 		:= -I../src/

C_FLAGS 	:= -std=c11	  -fPIC -Wall -O3 -g
CXX_FLAGS 	:= -std=c++17 -fPIC -Wall -O3 -Weffc++ -g

DEPFLAGS_DEFAULT	 = -MT $@ -MMD -MP
DEPFLAGS_C_ORIG 	 = $(DEPFLAGS_DEFAULT) -MF $(orig_d_dir)/c/$*.d
DEPFLAGS_CPP_ORIG 	 = $(DEPFLAGS_DEFAULT) -MF $(orig_d_dir)/cpp/$*.d


DEPFILES := $(ORIG_CPP_SOURCES:%.cpp=$(orig_d_dir)/cpp/%.d) $(ORIG_C_SOURCES:%.c=$(orig_d_dir)/c/%.d)
$(DEPFILES):
include $(wildcard $(DEPFILES))


.SECONDEXPANSION:

#  ------------------- Compile objects -------------------
$(orig_o_dir)/asm/%.o : %.asm makefile_builder.mk | $$(@D)/.
	@$(ECHO) $(ANSI_YELLOW) ----------- Assembling : $@ ----------------- $(ANSI_DEFAULT)
	nasm -w+all $(ASM_FLAGS) $< -o $@

$(orig_o_dir)/c/%.o : %.c makefile_builder.mk | $$(@D)/. $(orig_d_dir)/c/$$(*D)/.
	@$(ECHO) $(ANSI_YELLOW) ----------- Compiling : $@ ----------------- $(ANSI_DEFAULT)
	gcc $(C_FLAGS) $(DEPFLAGS_C_ORIG) $(INCLUDE_DIRS) -c $< -o $@

$(orig_o_dir)/cpp/%.o : %.cpp makefile_builder.mk | $$(@D)/. $(orig_d_dir)/cpp/$$(*D)/.
	@$(ECHO) $(ANSI_YELLOW) ----------- Compiling : $@ ----------------- $(ANSI_DEFAULT)
	g++ $(CXX_FLAGS) $(DEPFLAGS_CPP_ORIG) $(INCLUDE_DIRS) -c $< -o $@

#  ------------------- link final target -------------------
$(TARGET): $(ORIG_ASM_OBJS) $(ORIG_C_OBJS) $(ORIG_CPP_OBJS) makefile | $$(@D)/.
	@$(ECHO) $(ANSI_GREEN) ----------- Linking : $@ ----------------- $(ANSI_DEFAULT)
	g++ $(ORIG_ASM_OBJS) $(ORIG_C_OBJS) $(ORIG_CPP_OBJS) $(LINKER_FLAGS) -o $@ -s
	@$(ECHO) $(ANSI_RED) ----------- DONE: Target $@ ----------- $(ANSI_DEFAULT)