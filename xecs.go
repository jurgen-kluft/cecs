package main

import (
	xcode "github.com/jurgen-kluft/xcode"
	pkg "github.com/jurgen-kluft/xecs/package"
)

func main() {
	xcode.Init()
	xcode.Generate(pkg.GetPackage())
}
