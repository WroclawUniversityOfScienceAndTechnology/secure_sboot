#
# Maintained locally and excluded from `pwrforge update` to fix STM32 sourcing.
#
import os

from conan import ConanFile
from conan.tools.cmake import CMake, cmake_layout
from conan.tools.files import copy, get
from conan.tools.scm import Git

class BootloaderConan(ConanFile):  # type: ignore[misc, no-any-unimported]
    name = "bootloader"
    version = "0.1.0"
    license = "MIT"
    description = "Secure fail-safe secondary boorloader"
    settings = "os", "compiler", "build_type", "arch"
    url = "pwrforge.pwr.edu.pl"
    generators = "CMakeToolchain", "CMakeDeps"
    exports_sources = ["src/*", "CMakeLists.txt", "include/*", "config/*"]

    def source(self) -> None:
        self._source_stm32()

    def layout(self) -> None:
        cmake_layout(self)

    def build(self) -> None:
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self) -> None:
        copy("*.bin")
        copy("*.elf")

    def _source_stm32(self) -> None:
        target_dir = os.path.join(self.source_folder, "third-party", "stm32-cmake")
        # Source archives may contain this directory without the dependency files.
        if not os.path.isfile(os.path.join(target_dir, "cmake", "stm32_gcc.cmake")):
            self.output.info("Sourcing for stm32")
            git = Git(self)
            git.clone("https://github.com/ObKo/stm32-cmake.git", target=target_dir)

