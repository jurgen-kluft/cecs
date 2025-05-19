package cecs

import (
	cbase "github.com/jurgen-kluft/cbase/package"
	denv "github.com/jurgen-kluft/ccode/denv"
	ccore "github.com/jurgen-kluft/ccore/package"
	cunittest "github.com/jurgen-kluft/cunittest/package"
)

// GetPackage returns the package object of 'cecs'
func GetPackage() *denv.Package {
	// Dependencies
	cunittestpkg := cunittest.GetPackage()
	ccorepkg := ccore.GetPackage()
	cbasepkg := cbase.GetPackage()

	// The main (cecs) package
	mainpkg := denv.NewPackage("cecs")
	mainpkg.AddPackage(cunittestpkg)
	mainpkg.AddPackage(ccorepkg)
	mainpkg.AddPackage(cbasepkg)

	// 'cecs' library
	mainlib := denv.SetupCppLibProject("cecs", "github.com\\jurgen-kluft\\cecs")
	mainlib.AddDependencies(cbasepkg.GetMainLib()...)
	mainlib.AddDependencies(ccorepkg.GetMainLib()...)

	// 'cecs' unittest project
	maintest := denv.SetupDefaultCppTestProject("cecs_test", "github.com\\jurgen-kluft\\cecs")
	maintest.AddDependencies(cunittestpkg.GetMainLib()...)
	maintest.Dependencies = append(maintest.Dependencies, mainlib)

	mainpkg.AddMainLib(mainlib)
	mainpkg.AddUnittest(maintest)
	return mainpkg
}
