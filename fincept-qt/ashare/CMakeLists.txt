cmake_minimum_required(VERSION 3.27)
project(AShareBoard VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets Network Test)
set(AS_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../src")
add_library(ashare_board STATIC
    ${AS_SRC}/services/ashare/AShareResearchService.cpp
    ${AS_SRC}/services/ashare/AShareResearchService.h
    ${AS_SRC}/screens/ashare/AShareResearchPanel.cpp
    ${AS_SRC}/screens/ashare/AShareResearchPanel.h
    ${AS_SRC}/services/ashare/AShareMarketService.cpp
    ${AS_SRC}/services/ashare/AShareMarketService.h
    ${AS_SRC}/services/ashare/AShareIndicators.cpp
    ${AS_SRC}/services/ashare/AShareIndicators.h
    ${AS_SRC}/services/ashare/AShareProfileService.cpp
    ${AS_SRC}/services/ashare/AShareProfileService.h
    ${AS_SRC}/services/llm/ProviderCatalog.cpp
    ${AS_SRC}/services/llm/ModelCatalog.cpp
    ${AS_SRC}/services/ashare/AShareAiService.cpp
    ${AS_SRC}/services/ashare/AShareAiService.h
    ${AS_SRC}/services/ashare/AShareLedger.cpp
    ${AS_SRC}/services/ashare/AShareLedger.h
    ${AS_SRC}/services/ashare/AShareNotes.cpp
    ${AS_SRC}/services/ashare/AShareNotes.h
    ${AS_SRC}/screens/ashare/AShareAiPanel.cpp
    ${AS_SRC}/screens/ashare/AShareAiPanel.h
    ${AS_SRC}/screens/ashare/ASharePortfolioPanel.cpp
    ${AS_SRC}/screens/ashare/ASharePortfolioPanel.h
    ${AS_SRC}/screens/ashare/AShareNotesPanel.cpp
    ${AS_SRC}/screens/ashare/AShareNotesPanel.h
    ${AS_SRC}/services/ashare/AShareOverviewService.cpp
    ${AS_SRC}/services/ashare/AShareOverviewService.h
    ${AS_SRC}/screens/ashare/AShareMarketPanel.cpp
    ${AS_SRC}/screens/ashare/AShareMarketPanel.h
    ${AS_SRC}/screens/ashare/AShareOrderBook.cpp
    ${AS_SRC}/screens/ashare/AShareOrderBook.h
    ${AS_SRC}/screens/ashare/AShareProfilePanel.cpp
    ${AS_SRC}/screens/ashare/AShareProfilePanel.h
    ${AS_SRC}/screens/ashare/AShareChart.cpp
    ${AS_SRC}/screens/ashare/AShareChart.h
    ${AS_SRC}/screens/ashare/AShareScreen.cpp
    ${AS_SRC}/screens/ashare/AShareScreen.h)
target_include_directories(ashare_board PUBLIC "${AS_SRC}")
target_link_libraries(ashare_board PUBLIC Qt6::Widgets Qt6::Network)
if(WIN32)
    target_link_libraries(ashare_board PUBLIC crypt32) # DPAPI for the AI API key
endif()
if(MSVC)
    target_compile_options(ashare_board PUBLIC /utf-8)
else()
    target_compile_options(ashare_board PRIVATE -Wall -Wextra)
endif()
qt_add_executable(AShareBoard WIN32 main.cpp)
target_link_libraries(AShareBoard PRIVATE ashare_board)
enable_testing()
qt_add_executable(AShareTests tests.cpp)
target_sources(AShareTests PRIVATE ${AS_SRC}/core/layout/LayoutTemplates.cpp ${AS_SRC}/core/layout/LayoutTypes.cpp)
target_link_libraries(AShareTests PRIVATE ashare_board Qt6::Test)
add_test(NAME AShareTests COMMAND AShareTests -platform offscreen)
