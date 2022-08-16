package main

import (
	ccode "github.com/jurgen-kluft/ccode"
	cpkg "github.com/jurgen-kluft/cecs/package"
)

func main() {
	ccode.Init()
	ccode.Generate(cpkg.GetPackage())
}
