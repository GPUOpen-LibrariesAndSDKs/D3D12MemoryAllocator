set(D3D12MA_LIBRARY_SOURCE_FILES
    D3D12MemAlloc.cpp
    "${PROJECT_SOURCE_DIR}/include/D3D12MemAlloc.h"
)

if(WIN32 AND ${CMAKE_GENERATOR} MATCHES "Visual Studio.*")
    set(D3D12MA_LIBRARY_SOURCE_FILES ${D3D12MA_LIBRARY_SOURCE_FILES} D3D12MemAlloc.natvis)
endif()

set(CMAKE_DEBUG_POSTFIX d)
set(CMAKE_RELWITHDEBINFO_POSTFIX rd)
set(CMAKE_MINSIZEREL_POSTFIX s)

add_library(D3D12MemoryAllocator ${D3D12MA_LIBRARY_SOURCE_FILES})

set_target_properties(
    D3D12MemoryAllocator PROPERTIES

    CXX_EXTENSIONS OFF
    # Use C++14
    CXX_STANDARD 14
    CXX_STANDARD_REQUIRED ON

    OUTPUT_NAME "D3D12MA"
)

target_link_libraries(D3D12MemoryAllocator PUBLIC
    d3d12.lib
    dxgi.lib
    dxguid.lib
)

if(BUILD_SHARED_LIBS)
    target_compile_definitions(D3D12MemoryAllocator PRIVATE
        D3D12MA_EXPORTS
    )

    target_compile_definitions(D3D12MemoryAllocator INTERFACE
        D3D12MA_IMPORTS
    )
endif()

install(TARGETS D3D12MemoryAllocator DESTINATION "lib")
install(FILES "${PROJECT_SOURCE_DIR}/include/D3D12MemAlloc.h" DESTINATION "include")

if(D3D12MA_BUILD_SAMPLE)
    if(WIN32)
        set(SHADER_DIR "Shaders")
        
        set(D3D12_SAMPLE_SOURCE_FILES
            Common.cpp
            Common.h
            Tests.cpp
            Tests.h
            D3D12Sample.cpp
        )
		
		set(VERTEX_SHADERS 
			"${SHADER_DIR}/VS.hlsl"
		)
				
		set(PIXEL_SHADERS
			"${SHADER_DIR}/PS.hlsl"
		)

		set( SHADERS
			${VERTEX_SHADERS}
			${PIXEL_SHADERS}
		)

		source_group("Resources\\Shaders" FILES ${SHADERS})

		set_source_files_properties(${VERTEX_SHADERS}
			PROPERTIES 
				VS_SHADER_TYPE Vertex
				VS_SETTINGS "ExcludedFromBuild=true"
		)

		set_source_files_properties( ${PIXEL_SHADERS}
			PROPERTIES 
				VS_SHADER_TYPE Pixel
				VS_SETTINGS "ExcludedFromBuild=true"
		)	

        add_executable(D3D12Sample ${D3D12_SAMPLE_SOURCE_FILES} ${SHADERS})

		add_dependencies(D3D12Sample D3D12MemoryAllocator)

        # Visual Studio specific settings
        if(${CMAKE_GENERATOR} MATCHES "Visual Studio.*")
            # Use Unicode instead of multibyte set
            add_compile_definitions(UNICODE _UNICODE)
            
            # Set VmaSample as startup project
            set_property(DIRECTORY ${PROJECT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT "D3D12Sample")
            
            # Enable multithreaded compiling
            target_compile_options(D3D12Sample PRIVATE "/MP")

            # Set working directory for Visual Studio debugger
            set_target_properties(
                D3D12Sample
                PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/bin"
            )
        endif()

        set_target_properties(
            D3D12Sample PROPERTIES

            CXX_EXTENSIONS OFF
            # Use C++14
            CXX_STANDARD 14
            CXX_STANDARD_REQUIRED ON
        )

        target_link_libraries(
            D3D12Sample

            PRIVATE D3D12MemoryAllocator
			PUBLIC d3d12.lib
			PUBLIC dxgi.lib
			PUBLIC dxguid.lib
			PUBLIC Shlwapi.lib
        )
    else()
        message(STATUS "D3D12Sample application is not supported to Linux")
    endif()
endif()
