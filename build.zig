const std = @import("std");
const build_utils = @import("build_utils.zig");

const TargetParts = struct {
    arch: []const u8,
    libc: []const u8,
};

const MbedtlsBuild = struct {
    install_step: *std.Build.Step,
    install_dir: []const u8,
};

fn getCmakeBuildType(optimize: std.builtin.OptimizeMode) []const u8 {
    return switch (optimize) {
        .Debug => "Debug",
        .ReleaseSafe => "RelWithDebInfo",
        .ReleaseFast => "Release",
        .ReleaseSmall => "MinSizeRel",
    };
}

fn getTargetParts(target: std.Build.ResolvedTarget) TargetParts {
    if (target.result.os.tag != .linux) {
        std.debug.panic("libdatachannel wrapper supports linux targets only, got {s}", .{
            @tagName(target.result.os.tag),
        });
    }

    const arch = switch (target.result.cpu.arch) {
        .x86_64 => "x86_64",
        .aarch64 => "aarch64",
        else => std.debug.panic("unsupported cpu arch for libdatachannel: {s}", .{
            @tagName(target.result.cpu.arch),
        }),
    };

    const libc = switch (target.result.abi) {
        .gnu, .gnueabi, .gnueabihf => "gnu",
        .musl, .musleabi, .musleabihf => "musl",
        else => std.debug.panic("unsupported abi for libdatachannel: {s}", .{
            @tagName(target.result.abi),
        }),
    };

    return .{
        .arch = arch,
        .libc = libc,
    };
}

fn addMbedtlsBuild(
    b: *std.Build,
    target: std.Build.ResolvedTarget,
    optimize: std.builtin.OptimizeMode,
) MbedtlsBuild {
    const target_str = build_utils.getTargetString(target);
    const cmake_build_type = getCmakeBuildType(optimize);
    const target_parts = getTargetParts(target);
    const target_triple = b.fmt("{s}-linux-{s}", .{ target_parts.arch, target_parts.libc });

    const cmake_build_dir = b.fmt(".zig-cache/mbedtls-dep/{s}/{s}", .{ target_str, cmake_build_type });
    const cmake_install_dir = b.fmt("{s}/install", .{cmake_build_dir});
    const cmake_install_dir_abs = b.pathFromRoot(cmake_install_dir);

    const configure = b.addSystemCommand(&[_][]const u8{
        "cmake",
        "-S",
        "../mbedtls/vendor/mbedtls",
        "-B",
        cmake_build_dir,
        "-G",
        "Ninja",
        b.fmt("-DCMAKE_BUILD_TYPE={s}", .{cmake_build_type}),
        "-DCMAKE_SYSTEM_NAME=Linux",
        b.fmt("-DCMAKE_SYSTEM_PROCESSOR={s}", .{target_parts.arch}),
        "-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY",
        "-DCMAKE_C_COMPILER=zig",
        "-DCMAKE_C_COMPILER_ARG1=cc",
        b.fmt("-DCMAKE_C_COMPILER_TARGET={s}", .{target_triple}),
        "-DCMAKE_C_FLAGS=-DMBEDTLS_SSL_DTLS_SRTP",
        "-DENABLE_TESTING=OFF",
        "-DENABLE_PROGRAMS=OFF",
        "-DGEN_FILES=ON",
        "-DMBEDTLS_FATAL_WARNINGS=OFF",
        "-DUSE_SHARED_MBEDTLS_LIBRARY=ON",
        "-DUSE_STATIC_MBEDTLS_LIBRARY=OFF",
    });
    configure.setName(b.fmt("configure mbedtls dep ({s})", .{target_str}));

    const build_cmd = b.addSystemCommand(&[_][]const u8{
        "cmake",
        "--build",
        cmake_build_dir,
        "--config",
        cmake_build_type,
        "--target",
        "lib",
        "--parallel",
    });
    build_cmd.setName(b.fmt("build mbedtls dep ({s})", .{target_str}));
    build_cmd.step.dependOn(&configure.step);

    const install_cmd = b.addSystemCommand(&[_][]const u8{
        "cmake",
        "--install",
        cmake_build_dir,
        "--prefix",
        cmake_install_dir_abs,
    });
    install_cmd.setName(b.fmt("install mbedtls dep ({s})", .{target_str}));
    install_cmd.step.dependOn(&build_cmd.step);

    return .{
        .install_step = &install_cmd.step,
        .install_dir = cmake_install_dir_abs,
    };
}

fn addLibDataChannelSharedBuild(
    b: *std.Build,
    target: std.Build.ResolvedTarget,
    optimize: std.builtin.OptimizeMode,
    lib_name: []const u8,
) *std.Build.Step.InstallFile {
    const target_str = build_utils.getTargetString(target);
    const cmake_build_type = getCmakeBuildType(optimize);
    const target_parts = getTargetParts(target);
    const target_triple = b.fmt("{s}-linux-{s}", .{ target_parts.arch, target_parts.libc });
    const cmake_build_dir = b.fmt(".zig-cache/libdatachannel-mbedtls/{s}/{s}", .{ target_str, cmake_build_type });
    const cmake_install_dir = b.fmt("{s}/install", .{cmake_build_dir});
    const mbedtls_build = addMbedtlsBuild(b, target, optimize);
    const mbedtls_prefix = mbedtls_build.install_dir;
    const mbedtls_cmake_dir = b.fmt("{s}/lib/cmake/MbedTLS", .{mbedtls_prefix});
    const mbedtls_include_dir = b.fmt("{s}/include", .{mbedtls_prefix});
    const mbedtls_lib_dir = b.fmt("{s}/lib", .{mbedtls_prefix});
    const mbedtls_lib = b.fmt("{s}/libmbedtls.so", .{mbedtls_lib_dir});
    const mbedcrypto_lib = b.fmt("{s}/libmbedcrypto.so", .{mbedtls_lib_dir});
    const mbedx509_lib = b.fmt("{s}/libmbedx509.so", .{mbedtls_lib_dir});

    const configure = b.addSystemCommand(&[_][]const u8{
        "cmake",
        "-S",
        "vendor/libdatachannel",
        "-B",
        cmake_build_dir,
        "-G",
        "Ninja",
        b.fmt("-DCMAKE_BUILD_TYPE={s}", .{cmake_build_type}),
        "-DCMAKE_SYSTEM_NAME=Linux",
        b.fmt("-DCMAKE_SYSTEM_PROCESSOR={s}", .{target_parts.arch}),
        "-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY",
        "-DCMAKE_C_COMPILER=zig",
        "-DCMAKE_C_COMPILER_ARG1=cc",
        "-DCMAKE_CXX_COMPILER=zig",
        "-DCMAKE_CXX_COMPILER_ARG1=c++",
        b.fmt("-DCMAKE_C_COMPILER_TARGET={s}", .{target_triple}),
        b.fmt("-DCMAKE_CXX_COMPILER_TARGET={s}", .{target_triple}),
        "-DCMAKE_C_FLAGS=-DMBEDTLS_SSL_DTLS_SRTP",
        "-DCMAKE_CXX_FLAGS=-DMBEDTLS_SSL_DTLS_SRTP",
        "-DBUILD_SHARED_LIBS=ON",
        "-DBUILD_SHARED_DEPS_LIBS=OFF",
        "-DPREFER_SYSTEM_LIB=OFF",
        "-DUSE_SYSTEM_SRTP=OFF",
        "-DUSE_SYSTEM_JUICE=OFF",
        "-DUSE_SYSTEM_USRSCTP=OFF",
        "-DUSE_SYSTEM_PLOG=OFF",
        "-DUSE_SYSTEM_JSON=OFF",
        "-DUSE_MBEDTLS=ON",
        "-DUSE_GNUTLS=OFF",
        "-DENABLE_MBEDTLS=ON",
        "-DENABLE_OPENSSL=OFF",
        "-DMBEDTLS=ON",
        "-DOPENSSL=OFF",
        b.fmt("-DCMAKE_PREFIX_PATH={s}", .{mbedtls_prefix}),
        b.fmt("-DMbedTLS_DIR={s}", .{mbedtls_cmake_dir}),
        b.fmt("-DMbedTLS_INCLUDE_DIR={s}", .{mbedtls_include_dir}),
        b.fmt("-DMbedTLS_LIBRARY={s}", .{mbedtls_lib}),
        b.fmt("-DMbedCrypto_LIBRARY={s}", .{mbedcrypto_lib}),
        b.fmt("-DMbedX509_LIBRARY={s}", .{mbedx509_lib}),
        "-DNO_EXAMPLES=ON",
        "-DNO_TESTS=ON",
        "-DRTC_UPDATE_VERSION_HEADER=OFF",
    });
    configure.setName(b.fmt("configure libdatachannel ({s})", .{target_str}));
    configure.step.dependOn(mbedtls_build.install_step);

    const build_cmd = b.addSystemCommand(&[_][]const u8{
        "cmake",
        "--build",
        cmake_build_dir,
        "--config",
        cmake_build_type,
        "--target",
        "datachannel",
        "--parallel",
    });
    build_cmd.setName(b.fmt("build datachannel ({s})", .{target_str}));
    build_cmd.step.dependOn(&configure.step);

    const install_cmd = b.addSystemCommand(&[_][]const u8{
        "cmake",
        "--install",
        cmake_build_dir,
        "--prefix",
        cmake_install_dir,
    });
    install_cmd.setName(b.fmt("install datachannel ({s})", .{target_str}));
    install_cmd.step.dependOn(&build_cmd.step);

    const built_library = b.fmt("{s}/lib/libdatachannel.so", .{cmake_install_dir});
    const installed_library_name = b.fmt("lib{s}.so", .{lib_name});
    const install_lib = b.addInstallFileWithDir(
        .{ .cwd_relative = built_library },
        .lib,
        installed_library_name,
    );
    install_lib.step.dependOn(&install_cmd.step);

    return install_lib;
}

fn addLibDataChannelWrapperBuild(
    b: *std.Build,
    target: std.Build.ResolvedTarget,
    optimize: std.builtin.OptimizeMode,
    wrapper_name: []const u8,
) *std.Build.Step.InstallArtifact {
    const wrapper = b.addLibrary(.{
        .name = wrapper_name,
        .linkage = .dynamic,
        .root_module = b.createModule(.{
            .root_source_file = b.path("src/main.zig"),
            .target = target,
            .optimize = optimize,
        }),
    });

    wrapper.linkLibC();
    wrapper.linkSystemLibrary("dl");
    wrapper.linkSystemLibrary("pthread");
    wrapper.addCSourceFile(.{
        .file = b.path("src/libdatachannel_wrapper.c"),
        .flags = &.{ "-std=c11", "-fPIC" },
    });
    wrapper.root_module.addIncludePath(b.path("include"));
    wrapper.root_module.addIncludePath(b.path("vendor/libdatachannel/include"));

    return b.addInstallArtifact(wrapper, .{});
}

fn buildForTarget(
    b: *std.Build,
    target: std.Build.ResolvedTarget,
    optimize: std.builtin.OptimizeMode,
    artifacts_dir: []const u8,
    hashes: *std.StringHashMap([]const u8),
    json_step: *build_utils.WriteJsonStep,
) void {
    const target_str = build_utils.getTargetString(target);
    const lib_name = build_utils.getLibName(std.heap.page_allocator, "datachannel", target_str);
    const install_lib = addLibDataChannelSharedBuild(b, target, optimize, lib_name);
    const wrapper_name = build_utils.getLibName(std.heap.page_allocator, "datachannel_wrapper", target_str);
    const install_wrapper = addLibDataChannelWrapperBuild(b, target, optimize, wrapper_name);

    const hash_step = build_utils.HashAndMoveStep.create(
        b,
        lib_name,
        target_str,
        artifacts_dir,
        hashes,
    );
    hash_step.step.dependOn(&install_lib.step);
    hash_step.step.dependOn(&install_wrapper.step);

    json_step.step.dependOn(&hash_step.step);
}

pub fn build(b: *std.Build) void {
    const optimize = b.standardOptimizeOption(.{});
    const artifacts_dir = "../../artifacts/libs";
    const json_path = "current.json";

    const build_all = b.option(bool, "all", "Build for all supported targets") orelse false;
    const ffi_only = b.option(bool, "ffi_only", "Build only libdatachannel_wrapper (skip upstream core build)") orelse false;

    if (ffi_only) {
        const target = b.standardTargetOptions(.{});
        const install_wrapper = addLibDataChannelWrapperBuild(b, target, optimize, "datachannel_wrapper");
        const install_wrapper_h = b.addInstallHeaderFile(
            b.path("include/libdatachannel_wrapper.h"),
            "libdatachannel_wrapper.h",
        );
        b.getInstallStep().dependOn(&install_wrapper.step);
        b.getInstallStep().dependOn(&install_wrapper_h.step);
        return;
    }

    if (build_all) {
        const hashes = build_utils.createHashMap(b);
        const json_step = build_utils.WriteJsonStep.create(b, hashes, json_path);

        for (build_utils.supported_targets) |query| {
            const target = b.resolveTargetQuery(query);
            buildForTarget(b, target, optimize, artifacts_dir, hashes, json_step);
        }

        b.default_step.dependOn(&json_step.step);
    } else {
        const target = b.standardTargetOptions(.{});
        const install_lib = addLibDataChannelSharedBuild(b, target, optimize, "datachannel");
        const install_wrapper = addLibDataChannelWrapperBuild(b, target, optimize, "datachannel_wrapper");
        const install_rtc_h = b.addInstallHeaderFile(
            b.path("vendor/libdatachannel/include/rtc/rtc.h"),
            "rtc/rtc.h",
        );
        const install_version_h = b.addInstallHeaderFile(
            b.path("vendor/libdatachannel/include/rtc/version.h"),
            "rtc/version.h",
        );
        const install_wrapper_h = b.addInstallHeaderFile(
            b.path("include/libdatachannel_wrapper.h"),
            "libdatachannel_wrapper.h",
        );

        b.getInstallStep().dependOn(&install_lib.step);
        b.getInstallStep().dependOn(&install_wrapper.step);
        b.getInstallStep().dependOn(&install_rtc_h.step);
        b.getInstallStep().dependOn(&install_version_h.step);
        b.getInstallStep().dependOn(&install_wrapper_h.step);
    }
}
