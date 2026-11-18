# PlatformIO pre-build script.
# The ststm32 stm32cube builder only passes -mthumb -mcpu=cortex-m4. The FreeRTOS
# ARM_CM4F port and the float feature extractor need the hardware FPU, and the
# HAL, the libraries, and newlib must all use the same float ABI. So the flags
# go into the global construction environment (compile, assemble, link).
# -ffp-contract=off stops GCC fusing a*b+c into VFMA, which keeps on-target
# feature arithmetic bit-identical to the host verification build (x86 SSE).
import os

Import("env")  # noqa: F821  (SCons-provided)

FPU = ["-mfpu=fpv4-sp-d16", "-mfloat-abi=hard"]

# The linker runs in the project directory; a relative map path avoids the
# space in the user's home path breaking the -Wl option.
build_dir = env.subst("$BUILD_DIR")  # noqa: F821
map_rel = os.path.relpath(os.path.join(build_dir, "firmware.map"), env.subst("$PROJECT_DIR")).replace("\\", "/")  # noqa: F821

env.Append(  # noqa: F821
    CCFLAGS=FPU + ["-ffp-contract=off", "-fno-math-errno"],   # inline VSQRT; no errno users
    ASFLAGS=FPU,
    LINKFLAGS=FPU + [
        "-Wl,-Map," + map_rel,
        "-Wl,--print-memory-usage",
    ],
)
