from conan import ConanFile
from conan.tools.cmake import cmake_layout


class QuickRecordConan(ConanFile):
    name = "quickrecord"
    settings = "os", "compiler", "build_type", "arch"
    generators = "CMakeDeps", "CMakeToolchain"

    def requirements(self):
        self.requires("spdlog/1.16.0")
        self.requires("gtest/1.15.0")

    def configure(self):
        self.options["*"].shared = False
        self.options["*"].fPIC = True

    def layout(self):
        cmake_layout(self)
