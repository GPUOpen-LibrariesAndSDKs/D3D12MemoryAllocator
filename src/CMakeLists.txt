set(D3D12MA_LIBRARY_SOURCE_FILES
    D3D12MemAlloc.h
    D3D12MemAlloc.cpp
)
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
)

target_link_libraries(D3D12MemoryAllocator PUBLIC
    d3d12.lib
    dxgi.lib
    dxguid.lib
)

install(TARGETS D3D12MemoryAllocator DESTINATION lib)
install(FILES D3D12MemAlloc.h DESTINATION include)

if (D3D12MA_BUILD_SAMPLE)
    if(WIN32)
        set(D3D12_SAMPLE_SOURCE_FILES
            Common.cpp
            Tests.cpp
            D3D12Sample.cpp
        )
		
		set( VERTEX_SHADERS 
			Shaders/VS.hlsl
		)
				
		set( PIXEL_SHADERS
			Shaders/PS.hlsl
		)

		set( SHADERS
			${VERTEX_SHADERS}
			${PIXEL_SHADERS}
		)

		source_group( "Resources\\Shaders" FILES ${SHADERS} )

		set_source_files_properties( ${VERTEX_SHADERS}
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