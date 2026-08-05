package cecs

import (
	"os"
	"path/filepath"
	"testing"

	"github.com/jurgen-kluft/go-ide/denv"
	"github.com/jurgen-kluft/go-ide/msdev"
)

func TestGenerateMsdevPackage(t *testing.T) {
	outputDir := t.TempDir()
	err := msdev.GeneratePackage(GetPackage(), msdev.WorkspaceOptions{
		OutputDir:    outputDir,
		BuildTarget:  denv.BuildTargetWindowsX64,
		BuildConfigs: []denv.BuildConfig{denv.NewDebugConfig()},
	})
	if err != nil {
		t.Fatalf("GeneratePackage() error = %v", err)
	}
	if _, err := os.Stat(filepath.Join(outputDir, "cecs.sln")); err != nil {
		t.Fatalf("generated solution: %v", err)
	}
}
