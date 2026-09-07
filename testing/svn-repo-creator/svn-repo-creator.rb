#!/usr/bin/env ruby
# encoding: UTF-8

#
# SVN Demo Repository Creator
#
# Part 1/5
#
# Creates a local SVN repository with:
#
#   trunk
#   branches
#   tags
#
# and imports a C++ demo application
#

require "fileutils"


############################################################
# Configuration
############################################################

ROOT = File.expand_path(File.dirname(__FILE__))

REPOSITORY_DIR =
  File.join(ROOT, "demo_repository")

TRUNK_WC =
  File.join(ROOT, "wc_trunk")

BRANCH_UI_WC =
  File.join(ROOT, "wc_feature_ui")

BRANCH_NETWORK_WC =
  File.join(ROOT, "wc_feature_network")

BRANCH_REFACTOR_WC =
  File.join(ROOT, "wc_feature_refactor")


#
# SVN file URL
#
# Spaces are escaped for URLs
#

REPO_URL =
  "file://" +
  REPOSITORY_DIR.gsub(" ", "%20")



############################################################
# Hilfsfunktionen
############################################################


#
# Shell-sicheres Quoting
#
def q(value)

    "'" +
            value.to_s.gsub("'", "'\\''") +
            "'"

end



#
# Run a command
#
def run(command)

    puts
    puts "===================================================="
    puts command
    puts "===================================================="

    result = system(command)

    unless result

        abort(
            "\nKommando fehlgeschlagen:\n#{command}"
        )

    end

end



#
# Write a file
#
def write_file(path, content)

    FileUtils.mkdir_p(
        File.dirname(path)
    )

    File.open(path,"w") do |f|

        f.write(content)

    end

end



#
# Append to a file
#
def append_file(path, content)

    File.open(path,"a") do |f|

        f.write(content)

    end

end



############################################################
# Cleanup
############################################################

puts "Cleanup"

[
    REPOSITORY_DIR,
    TRUNK_WC,
    BRANCH_UI_WC,
    BRANCH_NETWORK_WC,
    BRANCH_REFACTOR_WC
].each do |path|

    FileUtils.rm_rf(path)

end



############################################################
# Create the SVN repository
############################################################


run(
    "svnadmin create #{q(REPOSITORY_DIR)}"
)



############################################################
# Standard SVN Struktur
############################################################


run(
    "svn mkdir " \
"#{REPO_URL}/trunk " \
"#{REPO_URL}/branches " \
"#{REPO_URL}/tags " \
"-m \"Create standard repository layout\""
)



############################################################
# Trunk Checkout
############################################################


run(
    "svn checkout " \
"#{REPO_URL}/trunk " \
"#{q(TRUNK_WC)}"
)



############################################################
# Projektstruktur
############################################################


directories = [

    "src",
    "include",
    "tests",
    "docs"

]


directories.each do |dir|

    FileUtils.mkdir_p(
        File.join(TRUNK_WC,dir)
    )

end



############################################################
# CMake file
############################################################


write_file(

    File.join(TRUNK_WC,"CMakeLists.txt"),

    <<~CMAKE

    cmake_minimum_required(VERSION 3.15)

    project(SVN_Demo_App)

    set(CMAKE_CXX_STANDARD 17)

    include_directories(include)


    add_executable(
    demo
    src/main.cpp
    src/Application.cpp
    src/Logger.cpp
    src/Math.cpp
    src/StringUtil.cpp
    src/Network.cpp
    )


CMAKE

)



############################################################
# Header Dateien
############################################################


headers = {


    "Application.hpp" => <<~CPP,

    #pragma once

    class Application
    {
    public:

    void run();

    };

CPP


"Logger.hpp" => <<~CPP,

#pragma once

#include <string>


class Logger
{

public:

void info(
const std::string&
);

};

CPP


"Math.hpp" => <<~CPP,

#pragma once


class Math
{

public:

int add(
int a,
int b
);


int multiply(
int a,
int b
);

};

CPP


"StringUtil.hpp" => <<~CPP,

#pragma once

#include <string>


class StringUtil
{

public:

std::string upper(
const std::string&
);

};

CPP


"Network.hpp" => <<~CPP,

#pragma once


class Network
{

public:

bool connect();

};

CPP


"Config.hpp" => <<~CPP

#pragma once

#define VERSION "1.0"

CPP


}



headers.each do |name,content|

    write_file(

        File.join(
                  TRUNK_WC,
                  "include",
                  name
                 ),

        content

    )

end



############################################################
# C++ Implementierungen
############################################################


sources = {


    "main.cpp" => <<~CPP,

    #include "Application.hpp"


    int main()
    {

    Application app;

    app.run();

    return 0;

    }

CPP



"Application.cpp" => <<~CPP,

#include "Application.hpp"
#include "Logger.hpp"


void Application::run()
{

Logger logger;

logger.info(
"Demo application started"
);

}

CPP



"Logger.cpp" => <<~CPP,

#include "Logger.hpp"

#include <iostream>


void Logger::info(
const std::string& text
)
{

std::cout
<< text
<< std::endl;

}

CPP



"Math.cpp" => <<~CPP,

#include "Math.hpp"


int Math::add(
int a,
int b
)
{

return a+b;

}



int Math::multiply(
int a,
int b
)
{

return a*b;

}

CPP



"StringUtil.cpp" => <<~CPP,

#include "StringUtil.hpp"


std::string StringUtil::upper(
const std::string& value
)
{

return value;

}

CPP



"Network.cpp" => <<~CPP

#include "Network.hpp"


bool Network::connect()
{

return true;

}

CPP


}



sources.each do |name,content|

    write_file(

        File.join(
                  TRUNK_WC,
                  "src",
                  name
                 ),

        content

    )

end



############################################################
# Tests
############################################################


write_file(

    File.join(
              TRUNK_WC,
              "tests",
              "MathTest.cpp"
             ),

    <<~CPP

    #include "Math.hpp"


    int main()
    {

    Math math;

    return math.add(1,2);

    }

CPP

)



write_file(

    File.join(
              TRUNK_WC,
              "tests",
              "NetworkTest.cpp"
             ),

    <<~CPP

    #include "Network.hpp"


    int main()
    {

    Network n;

    return n.connect()
    ? 0
    : 1;

    }

CPP

)



############################################################
# Dokumentation
############################################################


write_file(

    File.join(
              TRUNK_WC,
              "docs",
              "architecture.md"
             ),

    <<~DOC

    # SVN Demo Application

    Example C++ application.

DOC

)



############################################################
# Initial Commit
############################################################


Dir.chdir(TRUNK_WC) do

    run(
        "svn add . --force"
    )


    run(
        "svn commit -m \"Initial C++ demo application\""
    )

end



puts
puts "Teil 1 abgeschlossen."
puts
puts "Repository:"
puts REPOSITORY_DIR
puts
puts "Trunk:"
puts TRUNK_WC
############################################################
# TEIL 2/5
#
# Trunk Weiterentwicklung
# Release 1.0
# Feature UI Branch
############################################################


puts
puts "========== TEIL 2 =========="



############################################################
# Trunk Commit 2
#
# Erweiterung Math-Modul
############################################################


append_file(

    File.join(
              TRUNK_WC,
              "include",
              "Math.hpp"
             ),

    <<~CPP


    int subtract(
    int a,
    int b
    );

CPP

)



append_file(

    File.join(
              TRUNK_WC,
              "src",
              "Math.cpp"
             ),

    <<~CPP



    int Math::subtract(
    int a,
    int b
    )
    {

    return a-b;

    }

CPP

)



Dir.chdir(TRUNK_WC) do

    run(
        "svn commit -m \"Extend math module with subtraction\""
    )

end



############################################################
# Trunk Commit 3
#
# Logger Verbesserung
############################################################


append_file(

    File.join(
              TRUNK_WC,
              "include",
              "Logger.hpp"
             ),

    <<~CPP


    void warning(
    const std::string&
    );


CPP

)



append_file(

    File.join(
              TRUNK_WC,
              "src",
              "Logger.cpp"
             ),

    <<~CPP


    void Logger::warning(
    const std::string& text
    )
    {

    std::cout
    << "WARNING: "
    << text
    << std::endl;

    }


CPP

)



Dir.chdir(TRUNK_WC) do

    run(
        "svn commit -m \"Add warning logging support\""
    )

end



############################################################
# Trunk Commit 4
#
# Tests erweitern
############################################################


append_file(

    File.join(
              TRUNK_WC,
              "tests",
              "MathTest.cpp"
             ),

    <<~CPP


    // Additional arithmetic tests

CPP

)



Dir.chdir(TRUNK_WC) do

    run(
        "svn commit -m \"Extend math test coverage\""
    )

end



############################################################
# Release 1.0 Tag
############################################################


puts
puts "Creating release 1.0"



run(

    "svn copy " \
"#{REPO_URL}/trunk " \
"#{REPO_URL}/tags/release-1.0 " \
"-m \"Create release 1.0\""

)



############################################################
# Feature Branch UI erzeugen
#
# Branch basiert auf Release 1 Stand
############################################################


puts
puts "Creating feature-ui branch"



run(

    "svn copy " \
"#{REPO_URL}/trunk " \
"#{REPO_URL}/branches/feature-ui " \
"-m \"Create feature-ui development branch\""

)



############################################################
# Branch Checkout
############################################################


run(

    "svn checkout " \
"#{REPO_URL}/branches/feature-ui " \
"#{q(BRANCH_UI_WC)}"

)



############################################################
# Branch Commit 1
#
# Neue UI Header
############################################################


write_file(

    File.join(
              BRANCH_UI_WC,
              "include",
              "ConsoleUI.hpp"
             ),

    <<~CPP

    #pragma once


    #include <string>


    class ConsoleUI
    {

    public:

    void print(
    const std::string&
    );

    };

CPP

)



write_file(

    File.join(
              BRANCH_UI_WC,
              "src",
              "ConsoleUI.cpp"
             ),

    <<~CPP

    #include "ConsoleUI.hpp"

    #include <iostream>


    void ConsoleUI::print(
    const std::string& text
    )
    {

    std::cout
    << text
    << std::endl;

    }

CPP

)



Dir.chdir(BRANCH_UI_WC) do

    run(
        "svn add . --force"
    )


    run(
        "svn commit -m \"Add console UI component\""
    )

end



############################################################
# Branch Commit 2
#
# Integration Application
############################################################


append_file(

    File.join(
              BRANCH_UI_WC,
              "src",
              "Application.cpp"
             ),

    <<~CPP


    // Console UI integration


CPP

)



Dir.chdir(BRANCH_UI_WC) do

    run(
        "svn commit -m \"Integrate console UI into application\""
    )

end



############################################################
# Branch Commit 3
#
# Tests
############################################################


write_file(

    File.join(
              BRANCH_UI_WC,
              "tests",
              "ConsoleUITest.cpp"
             ),

    <<~CPP

    #include "ConsoleUI.hpp"


    int main()
    {

    ConsoleUI ui;

    ui.print(
    "test"
    );

    return 0;

    }

CPP

)



Dir.chdir(BRANCH_UI_WC) do

    run(
        "svn add . --force"
    )


    run(
        "svn commit -m \"Add console UI tests\""
    )

end



############################################################
# Parallel Trunk Entwicklung
#
# While the branch exists
############################################################


append_file(

    File.join(
              TRUNK_WC,
              "src",
              "Network.cpp"
             ),

    <<~CPP


    // connection diagnostics added


CPP

)



Dir.chdir(TRUNK_WC) do

    run(
        "svn commit -m \"Add network diagnostics\""
    )

end



append_file(

    File.join(
              TRUNK_WC,
              "include",
              "Config.hpp"
             ),

    <<~CPP


    #define BUILD_TYPE "development"


CPP

)



Dir.chdir(TRUNK_WC) do

    run(
        "svn commit -m \"Extend build configuration\""
    )

end



puts
puts "Teil 2 abgeschlossen."
############################################################
# TEIL 3/5
#
# Reverse Merge
# Feature UI Merge
# Release 2.0
# Feature Network Branch
############################################################


puts
puts "========== TEIL 3 =========="



############################################################
# Reverse Merge:
#
# Bring the current trunk state into feature-ui
#
# trunk
#   |
#   v
# feature-ui
############################################################


puts
puts "Synchronize feature-ui with trunk"



Dir.chdir(BRANCH_UI_WC) do

    run(
        "svn update"
    )


    run(
        "svn merge #{REPO_URL}/trunk"
    )

end



Dir.chdir(BRANCH_UI_WC) do

    run(
        "svn commit " \
    "-m \"Merge latest trunk changes into feature-ui\""
    )

end



############################################################
# Feature UI letzte Erweiterung
############################################################


append_file(

    File.join(
              BRANCH_UI_WC,
              "include",
              "ConsoleUI.hpp"
             ),

    <<~CPP


    void clear();


CPP

)



append_file(

    File.join(
              BRANCH_UI_WC,
              "src",
              "ConsoleUI.cpp"
             ),

    <<~CPP


    void ConsoleUI::clear()
    {

    std::cout
    << "clear"
    << std::endl;

    }


CPP

)



Dir.chdir(BRANCH_UI_WC) do

    run(
        "svn commit " \
    "-m \"Add console clear command\""
    )

end



############################################################
# Merge feature-ui back into trunk
############################################################


puts
puts "Merge feature-ui into trunk"



Dir.chdir(TRUNK_WC) do

    run(
        "svn update"
    )


    run(
        "svn merge #{REPO_URL}/branches/feature-ui"
    )

end



Dir.chdir(TRUNK_WC) do

    run(
        "svn commit " \
    "-m \"Merge feature-ui into trunk\""
    )

end



############################################################
# Trunk Weiterentwicklung nach UI Merge
############################################################


append_file(

    File.join(
              TRUNK_WC,
              "src",
              "Application.cpp"
             ),

    <<~CPP


    // improved application lifecycle


CPP

)



Dir.chdir(TRUNK_WC) do

    run(
        "svn commit " \
    "-m \"Improve application lifecycle handling\""
    )

end



############################################################
# Release 2.0 Tag
############################################################


puts
puts "Creating release 2.0"



run(

    "svn copy " \
"#{REPO_URL}/trunk " \
"#{REPO_URL}/tags/release-2.0 " \
"-m \"Create release 2.0\""

)



############################################################
# Neuer Branch:
#
# feature-network
############################################################


puts
puts "Creating feature-network branch"



run(

    "svn copy " \
"#{REPO_URL}/trunk " \
"#{REPO_URL}/branches/feature-network " \
"-m \"Create feature-network branch\""

)



############################################################
# Checkout Network Branch
############################################################


run(

    "svn checkout " \
"#{REPO_URL}/branches/feature-network " \
"#{q(BRANCH_NETWORK_WC)}"

)



############################################################
# Network Branch Commit 1
############################################################


write_file(

    File.join(
              BRANCH_NETWORK_WC,
              "include",
              "ConnectionManager.hpp"
             ),

    <<~CPP

    #pragma once


    class ConnectionManager
    {

    public:

    bool open();

    void close();

    };

CPP

)



write_file(

    File.join(
              BRANCH_NETWORK_WC,
              "src",
              "ConnectionManager.cpp"
             ),

    <<~CPP

    #include "ConnectionManager.hpp"


    bool ConnectionManager::open()
    {

    return true;

    }



    void ConnectionManager::close()
    {


    }

CPP

)



Dir.chdir(BRANCH_NETWORK_WC) do

    run(
        "svn add . --force"
    )


    run(
        "svn commit " \
    "-m \"Add connection manager\""
    )

end



############################################################
# Network Branch Commit 2
############################################################


append_file(

    File.join(
              BRANCH_NETWORK_WC,
              "src",
              "Network.cpp"
             ),

    <<~CPP


    // connection manager integration


CPP

)



Dir.chdir(BRANCH_NETWORK_WC) do

    run(
        "svn commit " \
    "-m \"Integrate connection manager\""
    )

end



############################################################
# Network Branch Commit 3
#
# Tests
############################################################


write_file(

    File.join(
              BRANCH_NETWORK_WC,
              "tests",
              "ConnectionTest.cpp"
             ),

    <<~CPP

    #include "ConnectionManager.hpp"


    int main()
    {

    ConnectionManager c;

    return c.open()
    ? 0
    : 1;

    }

CPP

)



Dir.chdir(BRANCH_NETWORK_WC) do

    run(
        "svn add . --force"
    )


    run(
        "svn commit " \
    "-m \"Add connection manager tests\""
    )

end



############################################################
# Parallel Trunk Entwicklung
############################################################


append_file(

    File.join(
              TRUNK_WC,
              "src",
              "Logger.cpp"
             ),

    <<~CPP


    // improved diagnostic output


CPP

)



Dir.chdir(TRUNK_WC) do

    run(
        "svn commit " \
    "-m \"Improve diagnostic logging\""
    )

end



append_file(

    File.join(
              TRUNK_WC,
              "tests",
              "NetworkTest.cpp"
             ),

    <<~CPP


    // prepare integration tests


CPP

)



Dir.chdir(TRUNK_WC) do

    run(
        "svn commit " \
    "-m \"Prepare integration testing\""
    )

end



puts
puts "Teil 3 abgeschlossen."
############################################################
# TEIL 4/5
#
# Merge feature-network
# Release 3.0
# Letzter Refactoring Branch
############################################################


puts
puts "========== TEIL 4 =========="



############################################################
# Network Branch -> Trunk
############################################################


puts
puts "Merge feature-network into trunk"



Dir.chdir(TRUNK_WC) do

    run(
        "svn update"
    )


    run(
        "svn merge #{REPO_URL}/branches/feature-network"
    )

end



Dir.chdir(TRUNK_WC) do

    run(
        "svn commit " \
    "-m \"Merge feature-network into trunk\""
    )

end



############################################################
# Trunk Weiterentwicklung vor Release 3
############################################################


append_file(

    File.join(
              TRUNK_WC,
              "src",
              "Network.cpp"
             ),

    <<~CPP


    // retry mechanism added


CPP

)



append_file(

    File.join(
              TRUNK_WC,
              "include",
              "Network.hpp"
             ),

    <<~CPP


    int retryCount();


CPP

)



Dir.chdir(TRUNK_WC) do

    run(
        "svn commit " \
    "-m \"Add network retry support\""
    )

end



############################################################
# Weitere Tests
############################################################


append_file(

    File.join(
              TRUNK_WC,
              "tests",
              "ConnectionTest.cpp"
             ),

    <<~CPP


    // extended connection tests


CPP

)



Dir.chdir(TRUNK_WC) do

    run(
        "svn commit " \
    "-m \"Extend connection tests\""
    )

end



############################################################
# Release 3.0
############################################################


puts
puts "Creating release 3.0"



run(

    "svn copy " \
"#{REPO_URL}/trunk " \
"#{REPO_URL}/tags/release-3.0 " \
"-m \"Create release 3.0\""

)



############################################################
# Letzten Branch erzeugen
#
# This branch is never merged
############################################################


puts
puts "Creating feature-refactor branch"



run(

    "svn copy " \
"#{REPO_URL}/trunk " \
"#{REPO_URL}/branches/feature-refactor " \
"-m \"Create refactor branch after release 3\""

)



############################################################
# Checkout Refactor Branch
############################################################


run(

    "svn checkout " \
"#{REPO_URL}/branches/feature-refactor " \
"#{q(BRANCH_REFACTOR_WC)}"

)



############################################################
# Refactor Commit 1
#
# Logger Abstraktion
############################################################


write_file(

    File.join(
              BRANCH_REFACTOR_WC,
              "include",
              "LogFormatter.hpp"
             ),

    <<~CPP

    #pragma once

    #include <string>


    class LogFormatter
    {

    public:

    std::string format(
    const std::string&
    );

    };

CPP

)



Dir.chdir(BRANCH_REFACTOR_WC) do

    run(
        "svn add . --force"
    )


    run(
        "svn commit " \
    "-m \"Introduce log formatter abstraction\""
    )

end



############################################################
# Refactor Commit 2
############################################################


write_file(

    File.join(
              BRANCH_REFACTOR_WC,
              "src",
              "LogFormatter.cpp"
             ),

    <<~CPP

    #include "LogFormatter.hpp"


    std::string LogFormatter::format(
    const std::string& text
    )
    {

    return text;

    }

CPP

)



Dir.chdir(BRANCH_REFACTOR_WC) do

    run(
        "svn add . --force"
    )


    run(
        "svn commit " \
    "-m \"Implement log formatter\""
    )

end



############################################################
# Refactor Commit 3
#
# Logger Umbau
############################################################


append_file(

    File.join(
              BRANCH_REFACTOR_WC,
              "src",
              "Logger.cpp"
             ),

    <<~CPP


    // formatter based logging pipeline


CPP

)



Dir.chdir(BRANCH_REFACTOR_WC) do

    run(
        "svn commit " \
    "-m \"Refactor logger pipeline\""
    )

end



############################################################
# Refactor Commit 4
#
# Tests
############################################################


write_file(

    File.join(
              BRANCH_REFACTOR_WC,
              "tests",
              "LoggerFormatterTest.cpp"
             ),

    <<~CPP

    #include "LogFormatter.hpp"


    int main()
    {

    LogFormatter f;

    f.format(
    "test"
    );

    return 0;

    }

CPP

)



Dir.chdir(BRANCH_REFACTOR_WC) do

    run(
        "svn add . --force"
    )


    run(
        "svn commit " \
    "-m \"Add formatter unit tests\""
    )

end



puts
puts "Teil 4 abgeschlossen."

############################################################
# TEIL 5/5
#
# Konfliktvorbereitung
# Abschlussanalyse
############################################################


puts
puts "========== TEIL 5 =========="



############################################################
# IMPORTANT:
#
# feature-refactor now exists.
#
# Changes on the branch:
#
# Logger.cpp
# Logger.hpp
#
# are now changed independently on trunk.
#
# A later merge would produce conflicts.
#
############################################################



############################################################
# Trunk change 1
#
# Gleicher Bereich wie Refactor Branch
# Logger.cpp
############################################################


append_file(

    File.join(
              TRUNK_WC,
              "src",
              "Logger.cpp"
             ),

    <<~CPP


    // TRUNK VERSION:
    // asynchronous logging backend


CPP

)



Dir.chdir(TRUNK_WC) do

    run(
        "svn commit " \
    "-m \"Introduce asynchronous logger backend\""
    )

end



############################################################
# Trunk change 2
#
# Gleicher Header-Bereich
# Logger.hpp
############################################################


append_file(

    File.join(
              TRUNK_WC,
              "include",
              "Logger.hpp"
             ),

    <<~CPP


    class AsyncLogger
    {

    public:

    void flush();

    };


CPP

)



Dir.chdir(TRUNK_WC) do

    run(
        "svn commit " \
    "-m \"Add async logger interface\""
    )

end



############################################################
# Trunk change 3
#
# Application change
############################################################


append_file(

    File.join(
              TRUNK_WC,
              "src",
              "Application.cpp"
             ),

    <<~CPP


    // new startup sequence after release 3


CPP

)



Dir.chdir(TRUNK_WC) do

    run(
        "svn commit " \
    "-m \"Change application startup sequence\""
    )

end



############################################################
# Another refactor-branch change
#
# So the divergence becomes clear
############################################################


append_file(

    File.join(
              BRANCH_REFACTOR_WC,
              "src",
              "Logger.cpp"
             ),

    <<~CPP


    // BRANCH VERSION:
    // formatter based logger architecture


CPP

)



Dir.chdir(BRANCH_REFACTOR_WC) do

    run(
        "svn commit " \
    "-m \"Complete formatter logger migration\""
    )

end



############################################################
# Keine Merge Operation!
#
# feature-refactor bleibt absichtlich offen.
############################################################



puts
puts
puts "===================================================="
puts "FINAL REPOSITORY STATE"
puts "===================================================="



############################################################
# Repository Inhalt
############################################################


puts
puts "Branches:"
puts


run(
    "svn list #{REPO_URL}/branches"
)



puts
puts "Tags:"
puts


run(
    "svn list #{REPO_URL}/tags"
)



############################################################
# Trunk Historie
############################################################


puts
puts "Recent trunk history:"
puts


run(
    "svn log #{REPO_URL}/trunk --limit 25"
)



############################################################
# Letzter Branch Historie
############################################################


puts
puts "feature-refactor history:"
puts


run(
    "svn log #{REPO_URL}/branches/feature-refactor --limit 25"
)



############################################################
# Repository Info
############################################################


puts
puts "Repository information:"
puts


run(
    "svn info #{REPO_URL}"
)



############################################################
# Abschluss
############################################################


puts
puts "===================================================="
puts
puts " SVN DEMO REPOSITORY SUCCESSFULLY CREATED"
puts
puts " Repository:"
puts REPOSITORY_DIR
puts
puts " Trunk:"
puts TRUNK_WC
puts
puts " Open branch:"
puts "branches/feature-refactor"
puts
puts " NOTE:"
puts "The last branch was intentionally NOT merged."
puts "A future merge would create conflicts."
puts
puts "===================================================="
