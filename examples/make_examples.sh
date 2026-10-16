# builds against the single file build: run "make dist" in the repository
# root first, or put wrench.h and wrench.cpp from a release into ../dist
g++ -o basic -I../dist ../dist/wrench.cpp basic.cpp
g++ -o multi -I../dist ../dist/wrench.cpp multi_context.cpp
