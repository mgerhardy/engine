project(testshapebuilder)
set(SRCS
	TestShapeBuilder.h TestShapeBuilder.cpp
)
engine_add_executable(TARGET ${PROJECT_NAME} SRCS ${SRCS} WINDOWED NOINSTALL)
engine_target_link_libraries(TARGET ${PROJECT_NAME} DEPENDENCIES testcore)
