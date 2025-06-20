package main

import (
	"bytes"
	"context"
	"encoding/json"
	"encoding/xml"
	"flag"
	"fmt"
	"gopkg.in/yaml.v3"
	"io"
	"io/ioutil"
	"log"
	"math"
	"math/rand"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"runtime"
	"sort"
	"strconv"
	"strings"
	"sync"
	"time"
)

// --- Constants (Combined from various Python originals and Go conversions) ---
const (
	defaultTimeoutSeconds        = 5 * time.Minute // Corresponds to _DEFAULT_TIMEOUT_SECONDS in Python
	preBuildStepTimeoutSeconds   = 10 * time.Minute
	cppRunTestsTimeout           = 6 * time.Hour
	objcRunTestsTimeout          = 4 * time.Hour
	pythonWindowsRunTestsTimeout = time.Duration(1.5 * float64(time.Hour)) // FIXED: Type conversion
	rubyRunTestsTimeout          = 2 * time.Hour
	defaultInnerJobs             = 2 // From the matrix script, often passed to `run_tests.py`
)

// --- Global Variables and Maps ---
var (
	// Corresponds to Python's `flaky_tests = set()`
	flakyTests = make(map[string]bool)
	// Corresponds to Python's `shortname_to_cpu = {}`
	shortnameToCPU = make(map[string]float64)

	// _FORCE_ENVIRON_FOR_WRAPPERS equivalent
	forceEnvironForWrappers = map[string]string{
		"GRPC_VERBOSITY": "DEBUG",
	}

	// _POLLING_STRATEGIES equivalent
	pollingStrategies = map[string][]string{
		"linux": {"epoll1", "poll"},
		"mac":   {"poll"},
	}
	// _MSBUILD_CONFIG equivalent
	msbuildConfig = map[string]string{
		"dbg":  "Debug",
		"opt":  "Release",
		"gcov": "Debug",
	}

	// Global variable to hold parsed arguments, accessible throughout the main package
	parsedArgs Args

	// Language Registry
	languageRegistry = make(map[string]LanguageInterface)
)

// --- Command-Line Argument Parsing Structures and Helpers ---

// Args struct to hold parsed command-line arguments
type Args struct {
	Config                  string
	RunsPerTest             int
	Regex                   string
	RegexExclude            string
	Jobs                    int
	Slowdown                float64
	SamplePercent           float64
	Travis                  bool
	NewlineOnSuccess        bool
	Language                []string
	StopOnFailure           bool
	UseDocker               bool
	AllowFlakes             bool
	Arch                    string
	Compiler                string
	IOMgrPlatform           string
	BuildOnly               bool
	MeasureCPUCosts         bool
	Antagonists             int
	XMLReport               string
	ReportSuiteName         string
	ReportMultiTarget       bool
	QuietSuccess            bool
	ForceDefaultPoller      bool
	ForceUsePollers         string
	MaxTime                 int
	BQResultTable           string
	CMakeConfigureExtraArgs []string
	InnerJobs               int
}

// runsPerTestTypeFlag implements flag.Value for "runs_per_test" (supports "inf")
type runsPerTestType int

func (i *runsPerTestType) String() string {
	if *i == 0 {
		return "inf"
	}
	return fmt.Sprintf("%d", *i)
}

func (i *runsPerTestType) Set(s string) error {
	if s == "inf" {
		*i = 0
		return nil
	}
	n, err := fmt.Sscanf(s, "%d", i)
	if err != nil || n < 0 { // Changed to n < 0 to allow 0 for 'inf' case
		return fmt.Errorf("'%s' is not a positive integer or 'inf'", s)
	}
	return nil
}

// percentTypeFlag implements flag.Value for "sample_percent"
type percentType float64

func (f *percentType) String() string {
	return fmt.Sprintf("%f", *f)
}

func (f *percentType) Set(s string) error {
	pct, err := strconv.ParseFloat(s, 64)
	if err != nil {
		return fmt.Errorf("invalid float '%s'", s)
	}
	if pct > 100 || pct < 0 {
		return fmt.Errorf("'%f' is not a valid percentage in the [0, 100] range", pct)
	}
	*f = percentType(pct)
	return nil
}

// stringSliceFlag implements flag.Value for a slice of strings (e.g., for --language, --cmake_configure_extra_args)
type stringSliceFlag []string

func (s *stringSliceFlag) String() string {
	return strings.Join(*s, ",")
}

func (s *stringSliceFlag) Set(value string) error {
	*s = append(*s, value)
	return nil
}

// --- Build and Run Error Types ---

type BuildAndRunError int

const (
	BuildError BuildAndRunError = iota
	TestError
	PostTestError
)

func (e BuildAndRunError) String() string {
	switch e {
	case BuildError:
		return "Build"
	case TestError:
		return "Test"
	case PostTestError:
		return "PostTest"
	default:
		return fmt.Sprintf("UnknownError(%d)", e)
	}
}

// --- Utility Functions (equivalent to python_utils/ various modules) ---

func platformString() string {
	// Corresponds to jobset.platform_string()
	switch runtime.GOOS {
	case "linux":
		return "linux"
	case "darwin":
		return "mac"
	case "windows":
		return "windows"
	default:
		return runtime.GOOS
	}
}

func runShellCommand(cmd string, env map[string]string, cwd string) error {
	// Simplified equivalent of Python's subprocess.check_output(shell=True)
	command := exec.Command("bash", "-c", cmd) // Assuming bash is available for shell=True behavior
	if runtime.GOOS == "windows" {
		command = exec.Command("cmd.exe", "/C", cmd)
	}

	command.Dir = cwd
	if env != nil {
		currentEnv := os.Environ()
		for k, v := range env {
			currentEnv = append(currentEnv, fmt.Sprintf("%s=%s", k, v))
		}
		command.Env = currentEnv
	}

	output, err := command.CombinedOutput()
	if err != nil {
		log.Printf("Error while running command '%s'. Exit status %d. Output:\n%s", cmd, command.ProcessState.ExitCode(), string(output))
		return fmt.Errorf("command failed: %w, output: %s", err, string(output))
	}
	return nil
}

func maxParallelTestsForCurrentPlatform() int {
	if platformString() == "windows" {
		return 64
	}
	return 1024
}

func printDebugInfoEpilogue(dockerfileDir string) {
	fmt.Println("")
	fmt.Println("=== run_tests.py DEBUG INFO ===")
	fmt.Printf("command: \"%s\"\n", strings.Join(os.Args, " "))
	if dockerfileDir != "" {
		fmt.Printf("dockerfile: %s\n", dockerfileDir)
	}
	if kokoroJobName := os.Getenv("KOKORO_JOB_NAME"); kokoroJobName != "" {
		fmt.Printf("kokoro job name: %s\n", kokoroJobName)
	}
	fmt.Println("===============================")
}

func checkCompiler(compiler string, supportedCompilers []string) error {
	for _, s := range supportedCompilers {
		if compiler == s {
			return nil
		}
	}
	return fmt.Errorf("compiler %s not supported (on this platform)", compiler)
}

func checkArch(arch string, supportedArchs []string) error {
	for _, s := range supportedArchs {
		if arch == s {
			return nil
		}
	}
	return fmt.Errorf("architecture %s not supported", arch)
}

func isUseDockerChild() bool {
	return os.Getenv("DOCKER_RUN_SCRIPT_COMMAND") != ""
}

func windowsArchOption(arch string) (string, error) {
	if arch == "default" || arch == "x86" {
		return "/p:Platform=Win32", nil
	} else if arch == "x64" {
		return "/p:Platform=x64", nil
	}
	return "", fmt.Errorf("architecture %s not supported on Windows", arch)
}

func checkArchOption(arch string) error {
	if platformString() == "windows" {
		_, err := windowsArchOption(arch)
		return err
	} else if platformString() == "linux" {
		runtimeMachine := runtime.GOARCH // aarch64, amd64 (x86_64)
		runtimeArch := runtime.GOARCH    // amd64, arm64

		if arch == "default" {
			return nil
		} else if runtimeMachine == "amd64" && runtimeArch == "amd64" && arch == "x64" {
			return nil
		} else if runtimeMachine == "amd64" && runtimeArch == "386" && arch == "x86" { // 32-bit on 64-bit machine
			// This check is tricky in Go. runtime.GOARCH will report the architecture of the Go binary.
			// To check the underlying OS architecture for a 32-bit binary on a 64-bit kernel,
			// you might need `uname -m` output or similar. For now, assume runtime.GOARCH is sufficient.
			return fmt.Errorf("architecture %s does not match current runtime architecture for x86", arch)
		} else if runtimeMachine == "arm64" && runtimeArch == "arm64" && arch == "arm64" {
			return nil
		} else {
			return fmt.Errorf("architecture %s does not match current runtime architecture (%s)", arch, runtimeMachine)
		}
	} else { // MacOS
		if arch != "default" {
			return fmt.Errorf("architecture %s not supported on current platform (%s)", arch, platformString())
		}
	}
	return nil
}

func dockerArchSuffix(arch string) string {
	if arch == "default" || arch == "x64" {
		return "x64"
	} else if arch == "x86" {
		return "x86"
	} else if arch == "arm64" {
		return "arm64"
	} else {
		log.Fatalf("Architecture %s not supported with current settings for Docker.", arch)
		return "" // Should not reach
	}
}

// isClose is Go's equivalent of Python's math.isclose
func isClose(a, b, relTol, absTol float64) bool {
	return math.Abs(a-b) <= math.Max(relTol*math.Max(math.Abs(a), math.Abs(b)), absTol)
}

// stringInSlice checks if a string is present in a slice of interface{}.
// Used for Python's `in target["exclude_configs"]` with mixed types.
func stringInSlice(s string, list []interface{}) bool {
	for _, v := range list {
		if str, ok := v.(string); ok && str == s {
			return true
		}
	}
	return false
}

// convertInterfaceSliceToStringSlice converts a []interface{} to []string
func convertInterfaceSliceToStringSlice(in []interface{}) []string {
	out := make([]string, len(in))
	for i, v := range in {
		if s, ok := v.(string); ok {
			out[i] = s
		} else {
			// Handle non-string types if necessary, or panic/error
			log.Printf("WARNING: Non-string element found in slice for conversion: %v", v)
			out[i] = fmt.Sprintf("%v", v) // Fallback to string representation
		}
	}
	return out
}

// quoteArgs is Go's equivalent of Python's shlex.quote
func quoteArgs(args []string) []string {
	quoted := make([]string, len(args))
	for i, arg := range args {
		// Simple quoting for shell safety. For complex cases, a proper shell escaping library would be needed.
		if strings.ContainsAny(arg, " \t\n\"'\\$`|&;<>*?[]#~()!{}") || len(arg) == 0 {
			quoted[i] = "'" + strings.ReplaceAll(arg, "'", "'\"'\"'") + "'"
		} else {
			quoted[i] = arg
		}
	}
	return quoted
}

func calculateNumRunsFailures(results []JobResult) (numRuns, numFailures int) {
	numRuns = len(results) // By default, there is 1 run per JobResult.
	numFailures = 0
	for _, jobResult := range results {
		// The original Python counts retries as additional runs
		if jobResult.Retries > 0 { // `Retries` here means 'number of times retried'
			numRuns += jobResult.Retries
		}
		// `NumFailures` field now correctly tracks total failed attempts for a spec, matching Python
		numFailures += jobResult.NumFailures
	}
	return
}

// getCTests (Simplified Python's get_c_tests)
// This function needs to read `tools/run_tests/generated/tests.json`
// and filter based on language, platform, and flaky status.
func getCTests(travis bool, testLang string) ([]map[string]interface{}, error) {
	testsJSONPath := filepath.Join(os.Getenv("GRPC_ROOT"), "tools", "run_tests", "generated", "tests.json")
	data, err := ioutil.ReadFile(testsJSONPath)
	if err != nil {
		return nil, fmt.Errorf("failed to read tests.json at %s: %w", testsJSONPath, err)
	}

	var allTests []map[string]interface{}
	// Python's ast.literal_eval is used in original, assuming JSON is standard here.
	if err := json.Unmarshal(data, &allTests); err != nil {
		return nil, fmt.Errorf("failed to unmarshal tests.json: %w", err)
	}

	var filteredTests []map[string]interface{}
	platformsStr := "ci_platforms"
	if !travis {
		platformsStr = "platforms"
	}

	currentPlatform := platformString()

	for _, tgt := range allTests {
		if lang, ok := tgt["language"].(string); !ok || lang != testLang {
			continue
		}

		if platforms, ok := tgt[platformsStr].([]interface{}); ok {
			platformFound := false
			for _, p := range platforms {
				if ps, ok := p.(string); ok && ps == currentPlatform {
					platformFound = true
					break
				}
			}
			if !platformFound {
				continue
			}
		} else {
			// If platforms_str key not found or not a list, skip or handle as appropriate.
			log.Printf("WARNING: Test target missing '%s' key or it's not a list: %v", platformsStr, tgt)
			continue
		}

		if travis {
			if flaky, ok := tgt["flaky"].(bool); ok && flaky {
				continue
			}
		}
		filteredTests = append(filteredTests, tgt)
	}
	return filteredTests, nil
}

// --- Job Runner (equivalent to python_utils/jobset.py) ---

// JobSpec represents a single job to be executed.
type JobSpec struct {
	Cmdline        []string
	Shortname      string
	EnvironMap     map[string]string // Use map for easier manipulation, converted to slice for exec.Cmd
	CPUCost        float64
	TimeoutSeconds time.Duration
	FlakeRetries   int // Number of times to retry a flaky job
	TimeoutRetries int // Number of times to retry a timed out job (Python has this separate)
}

// JobSpecOption allows setting JobSpec fields using functional options pattern.
type JobSpecOption func(*JobSpec)

func Shortname(s string) JobSpecOption { return func(js *JobSpec) { js.Shortname = s } }
func Environ(env map[string]string) JobSpecOption {
	return func(js *JobSpec) {
		js.EnvironMap = make(map[string]string)
		for k, v := range env {
			js.EnvironMap[k] = v
		}
	}
}
func CPUCost(c float64) JobSpecOption { return func(js *JobSpec) { js.CPUCost = c } }
func TimeoutSeconds(t float64) JobSpecOption {
	return func(js *JobSpec) { js.TimeoutSeconds = time.Duration(t * float64(time.Second)) }
}
func Flaky(f bool) JobSpecOption {
	return func(js *JobSpec) {
		if f || parsedArgs.AllowFlakes { // Global allow_flakes flag from Python
			js.FlakeRetries = 4
			js.TimeoutRetries = 1 // Python sets timeout_retries if flaky or allow_flakes
		} else {
			js.FlakeRetries = 0
			js.TimeoutRetries = 0
		}
	}
}

// JobResult represents the outcome of a single job execution (possibly after retries).
type JobResult struct {
	JobSpec JobSpec
	// Final outcome fields
	ExitCode int
	Output   string
	Error    error
	Duration time.Duration
	Skipped  bool // If job was skipped due to filtering or early exit
	// Internal tracking for retries, matching Python's jobset.JobResult
	Retries     int // Number of times the job was *actually* retried (successful retry attempts don't count as failures)
	NumFailures int // Total failures encountered for this job, including initial run and retries.
}

// MeasureCPUCosts global flag (corresponds to jobset.measure_cpu_costs)
var MeasureCPUCosts bool

// Run executes a list of JobSpecs concurrently.
func Run(
	jobs []JobSpec,
	maxJobs int,
	stopOnFailure bool,
	newlineOnSuccess bool,
	travis bool,
	quietSuccess bool,
	maxTime time.Duration, // Overall max time for all jobs
	allowFlakes bool, // Determines if flake_retries is enabled for jobs in this run
) (int, map[string][]JobResult) { // Returns (total failures, map of shortname to list of job results)
	if maxJobs <= 0 {
		maxJobs = 1
	}

	resultsChan := make(chan JobResult, len(jobs))
	var wg sync.WaitGroup
	resultSet := make(map[string][]JobResult)
	var resultSetMutex sync.Mutex // Protects access to resultSet

	workerPool := make(chan struct{}, maxJobs)

	ctx, cancel := context.WithCancel(context.Background())
	if maxTime > 0 {
		timer := time.AfterFunc(maxTime, func() {
			log.Printf("Overall max time (%v) reached. Cancelling remaining jobs.", maxTime)
			cancel()
		})
		defer timer.Stop()
	}
	defer cancel() // Ensure cancellation is called when function exits

	for _, job := range jobs {
		// Apply global allowFlakes setting to the job's retry counts if not already explicitly set.
		// This mirrors the Python logic: `flake_retries=4 if flaky or args.allow_flakes else 0`
		// and `timeout_retries=1 if flaky or args.allow_flakes else 0` in Config.job_spec
		// This should ideally be set when JobSpec is created, but added here as a fallback.
		if allowFlakes {
			if job.FlakeRetries == 0 { // Only override if not already configured by language spec
				job.FlakeRetries = 4
			}
			if job.TimeoutRetries == 0 {
				job.TimeoutRetries = 1
			}
		}

		wg.Add(1)
		select {
		case workerPool <- struct{}{}: // Acquire a slot
		case <-ctx.Done(): // If context is cancelled before slot available
			wg.Done()
			resultsChan <- JobResult{JobSpec: job, Skipped: true, ExitCode: -1, Error: fmt.Errorf("skipped due to overall timeout before starting")}
			continue
		}

		go func(j JobSpec) {
			defer wg.Done()
			defer func() { <-workerPool }() // Release the slot

			var jobResultsPerAttempt []JobResult // Store results for each attempt of this job
			numActualRetries := 0
			currentFailures := 0 // Failures for this specific job

			for attempt := 0; attempt <= j.FlakeRetries+j.TimeoutRetries; attempt++ {
				select {
				case <-ctx.Done():
					log.Printf("Skipping job %s (attempt %d) due to overall timeout.", j.Shortname, attempt)
					jobResultsPerAttempt = append(jobResultsPerAttempt, JobResult{JobSpec: j, Skipped: true, ExitCode: -1, Error: fmt.Errorf("skipped due to overall timeout")})
					goto finishJob // Exit retry loop
				default:
					// Continue
				}

				result := runSingleJob(j)
				jobResultsPerAttempt = append(jobResultsPerAttempt, result)

				if result.ExitCode == 0 {
					// Success, no more retries needed
					log.Printf("SUCCESS: %s (attempt %d, total retries %d)\n", j.Shortname, attempt+1, numActualRetries)
					break // Break out of retry loop
				}

				// If failed, increment currentFailures and consider retry
				currentFailures++
				log.Printf("Job %s failed (exit code %d) on attempt %d.\n", j.Shortname, result.ExitCode, attempt+1)

				isLastAttempt := attempt == (j.FlakeRetries + j.TimeoutRetries)
				if !isLastAttempt {
					numActualRetries++
					log.Printf("Retrying job %s... (total retries: %d)\n", j.Shortname, numActualRetries)
					time.Sleep(1 * time.Second) // Small delay before retry
				}
			}

		finishJob:
			// The final result for this JobSpec is the last attempt's result.
			// numActualRetries and currentFailures reflect the aggregate for this JobSpec.
			finalResult := jobResultsPerAttempt[len(jobResultsPerAttempt)-1]
			finalResult.Retries = numActualRetries    // Number of retries that actually happened
			finalResult.NumFailures = currentFailures // Total failures for this job spec's attempts

			resultsChan <- finalResult

			if stopOnFailure && finalResult.ExitCode != 0 && !finalResult.Skipped {
				log.Printf("Stop on failure enabled. Cancelling all jobs due to failure of %s.\n", j.Shortname)
				cancel() // Signal to cancel other jobs
			}
		}(job)
	}

	wg.Wait()
	close(resultsChan)

	// Aggregate results
	for res := range resultsChan {
		resultSetMutex.Lock()
		resultSet[res.JobSpec.Shortname] = append(resultSet[res.JobSpec.Shortname], res)
		resultSetMutex.Unlock()

		// Print messages to console (already done in runSingleJob for failure, but this is for overall)
		if res.Skipped {
			// Message already logged by goroutine: log.Printf("Skipping job %s ...")
		} else if res.ExitCode == 0 {
			if !quietSuccess {
				// Message already logged by goroutine: log.Printf("SUCCESS: %s ...")
			} else if newlineOnSuccess {
				fmt.Println() // Just a newline for quiet success
			}
		} else {
			// Message already logged by goroutine: log.Printf("Job %s failed ...")
		}
	}

	// Calculate total failures after all jobs are done
	totalFailures := 0
	for _, results := range resultSet {
		// A job is considered a "failure" if its last attempt failed.
		if len(results) > 0 && results[len(results)-1].ExitCode != 0 { // If the final attempt for this shortname failed
			totalFailures++
		}
	}

	return totalFailures, resultSet
}

// runSingleJob executes a single JobSpec.
func runSingleJob(job JobSpec) JobResult {
	start := time.Now()

	cmd := exec.Command(job.Cmdline[0], job.Cmdline[1:]...)

	// Prepare environment variables
	envList := os.Environ() // Start with current environment
	for k, v := range job.EnvironMap {
		envList = append(envList, fmt.Sprintf("%s=%s", k, v))
	}
	cmd.Env = envList

	var stdoutBuf, stderrBuf bytes.Buffer
	// Capture output to buffer and also pipe to os.Stdout/Stderr for real-time visibility
	cmd.Stdout = io.MultiWriter(os.Stdout, &stdoutBuf)
	cmd.Stderr = io.MultiWriter(os.Stderr, &stderrBuf)

	// Create and open a log file for this job
	// Ensure job_logs directory exists relative to GRPC_ROOT
	logFilename := filepath.Join(os.Getenv("GRPC_ROOT"), "job_logs", fmt.Sprintf("%s_%d.log", strings.ReplaceAll(job.Shortname, " ", "_"), time.Now().UnixNano()))
	logDir := filepath.Dir(logFilename)
	if err := os.MkdirAll(logDir, 0755); err != nil {
		log.Printf("Warning: Failed to create log directory %s for job %s: %v", logDir, job.Shortname, err)
	}
	logFile, err := os.Create(logFilename)
	if err != nil {
		log.Printf("Warning: Failed to create log file %s for job %s: %v", logFilename, job.Shortname, err)
	} else {
		defer logFile.Close()
		cmd.Stdout = io.MultiWriter(cmd.Stdout, logFile)
		cmd.Stderr = io.MultiWriter(cmd.Stderr, logFile)
	}

	err = cmd.Start()
	if err != nil {
		return JobResult{JobSpec: job, ExitCode: 1, Output: stdoutBuf.String() + stderrBuf.String(), Error: fmt.Errorf("failed to start command: %w", err)}
	}

	done := make(chan error, 1)
	go func() {
		done <- cmd.Wait()
	}()

	var cmdErr error
	select {
	case cmdErr = <-done:
		// Command finished within timeout
	case <-time.After(job.TimeoutSeconds):
		cmd.Process.Kill() // Force kill if timed out
		cmdErr = fmt.Errorf("job timed out after %v", job.TimeoutSeconds)
	}

	exitCode := 0
	if cmdErr != nil {
		if exitError, ok := cmdErr.(*exec.ExitError); ok {
			exitCode = exitError.ExitCode()
		} else {
			exitCode = 1 // General execution error (e.g., cmd not found, permission error)
		}
	}

	duration := time.Since(start)
	return JobResult{JobSpec: job, ExitCode: exitCode, Output: stdoutBuf.String() + stderrBuf.String(), Error: cmdErr, Duration: duration}
}

// --- Config Structures (equivalent to Python's Config class and its properties) ---

// Config represents a build configuration (e.g., "opt", "dbg").
type Config struct {
	BuildConfig       string            `json:"config"`
	Environ           map[string]string `json:"environ"`
	TimeoutMultiplier float64           `json:"timeout_multiplier"`
	ToolPrefix        []string          `json:"tool_prefix"`
	IOMgrPlatform     string            `json:"iomgr_platform"`
}

// LoadConfig loads a specific configuration from tools/run_tests/generated/configs.json.
func LoadConfig(configName string) (*Config, error) {
	configsFilePath := filepath.Join(os.Getenv("GRPC_ROOT"), "tools", "run_tests", "generated", "configs.json")
	data, err := ioutil.ReadFile(configsFilePath)
	if err != nil {
		return nil, fmt.Errorf("failed to read configs.json at %s: %w", configsFilePath, err)
	}

	var allConfigs []Config
	// Python's ast.literal_eval is used, which can parse JSON-like structures.
	// For simplicity, assuming the JSON is standard. If not, manual parsing or a custom unmarshaler is needed.
	if err := json.Unmarshal(data, &allConfigs); err != nil {
		return nil, fmt.Errorf("failed to unmarshal configs.json: %w", err)
	}

	for _, cfg := range allConfigs {
		if cfg.BuildConfig == configName {
			// Deep copy the environ map to prevent modifications by language configs
			copiedEnviron := make(map[string]string)
			if cfg.Environ != nil {
				for k, v := range cfg.Environ {
					copiedEnviron[k] = v
				}
			}
			cfg.Environ = copiedEnviron
			return &cfg, nil
		}
	}
	return nil, fmt.Errorf("config '%s' not found in %s", configName, configsFilePath)
}

// Config.JobSpec method
func (c *Config) JobSpec(cmdline []string, opts ...JobSpecOption) JobSpec {
	js := JobSpec{
		Cmdline:        append(c.ToolPrefix, cmdline...),
		EnvironMap:     make(map[string]string),
		CPUCost:        1.0,                                                                 // Default CPU cost
		TimeoutSeconds: time.Duration(c.TimeoutMultiplier * float64(defaultTimeoutSeconds)), // FIXED: Type conversion
	}

	// Copy base environment from config
	if c.Environ != nil {
		for k, v := range c.Environ {
			js.EnvironMap[k] = v
		}
	}
	// Add CONFIG env
	js.EnvironMap["CONFIG"] = c.BuildConfig

	// Apply functional options
	for _, opt := range opts {
		opt(&js)
	}

	// --- REVISED FLAKY LOGIC ---
	// This section mirrors Python's "if not flaky and shortname and shortname in flaky_tests: flaky = True"
	// If JobSpec was not explicitly marked as flaky (i.e., FlakeRetries is still 0 from options)
	// AND the shortname exists in the global flakyTests map.
	// The `Flaky` JobSpecOption already handles `parsedArgs.AllowFlakes`.
	// So, this block is for tests *not* explicitly marked flaky but *are* in the global flaky_tests list.
	if js.FlakeRetries == 0 && js.Shortname != "" && flakyTests[js.Shortname] {
		js.FlakeRetries = 4
		js.TimeoutRetries = 1
	}
	// --- END REVISED FLAKY LOGIC ---

	// Ensure timeouts are applied if they were somehow unset by an option.
	if js.TimeoutSeconds == 0 {
		js.TimeoutSeconds = time.Duration(c.TimeoutMultiplier * float64(defaultTimeoutSeconds)) // FIXED: Type conversion
	}

	// Apply global CPU cost overrides if not already set (e.g., by a CPUCost option)
	if cpu, ok := shortnameToCPU[js.Shortname]; ok {
		js.CPUCost = cpu
	}

	return js
}

// LanguageConfigTarget represents an entry from `tests.json` (for C/C++).
// Used internally by getCTests.
type LanguageConfigTarget struct {
	Name                string        `json:"name"`
	Language            string        `json:"language"`
	Platforms           []interface{} `json:"platforms"` // Can be string or array
	CIPlatforms         []interface{} `json:"ci_platforms"`
	ExcludeConfigs      []interface{} `json:"exclude_configs"`
	ExcludeIOMgrs       []interface{} `json:"exclude_iomgrs"`
	Args                []interface{} `json:"args"`
	CPUCost             interface{}   `json:"cpu_cost"` // Can be float or "capacity" string
	Flaky               bool          `json:"flaky"`
	AutoTimeoutScaling  bool          `json:"auto_timeout_scaling"`
	UsesPolling         bool          `json:"uses_polling"`
	ExcludedPollEngines []interface{} `json:"excluded_poll_engines"`
	TimeoutSeconds      float64       `json:"timeout_seconds"` // Optional
	Shortname           string        `json:"shortname"`       // Optional
	Benchmark           bool          `json:"benchmark"`       // Optional
	Gtest               bool          `json:"gtest"`           // Optional
	Boringssl           bool          `json:"boringssl"`       // Optional
}

// --- Reporter (equivalent to python_utils/report_utils.py) ---

// JUnit XML structures
type JUnitProperty struct {
	Name  string `xml:"name,attr"`
	Value string `xml:"value,attr"`
}

type JUnitFailure struct {
	Message string `xml:"message,attr"`
	Type    string `xml:"type,attr"`
	Content string `xml:",chardata"`
}

type JUnitTestCase struct {
	Name      string        `xml:"name,attr"`
	ClassName string        `xml:"classname,attr"`
	Time      float64       `xml:"time,attr"`
	Failure   *JUnitFailure `xml:"failure,omitempty"`
	Skipped   *struct{}     `xml:"skipped,omitempty"`
	SystemOut string        `xml:"system-out,omitempty"`
	SystemErr string        `xml:"system-err,omitempty"`
}

type JUnitTestSuite struct {
	Name       string          `xml:"name,attr"`
	Tests      int             `xml:"tests,attr"`
	Failures   int             `xml:"failures,attr"`
	Errors     int             `xml:"errors,attr"`
	Skipped    int             `xml:"skipped,attr"`
	Time       float64         `xml:"time,attr"`
	Properties []JUnitProperty `xml:"properties>property,omitempty"`
	TestCases  []JUnitTestCase `xml:"testcase"`
	SystemOut  string          `xml:"system-out,omitempty"`
	SystemErr  string          `xml:"system-err,omitempty"`
}

type JUnitReport struct {
	XMLName    xml.Name         `xml:"testsuites"`
	TestSuites []JUnitTestSuite `xml:"testsuite"`
}

// RenderJUnitXMLReport generates a JUnit-compatible XML report.
func RenderJUnitXMLReport(resultSet map[string][]JobResult, filename string, suiteName string, multiTarget bool) error {
	var testSuites []JUnitTestSuite

	if multiTarget {
		// Each shortname corresponds to a separate testsuite containing its attempts.
		for shortname, results := range resultSet {
			testSuite := JUnitTestSuite{
				Name: shortname,
				Time: 0.0, // Aggregated time for the suite
			}
			totalTests := 0
			totalFailures := 0
			totalSkipped := 0

			for _, result := range results {
				tc := JUnitTestCase{
					Name:      result.JobSpec.Shortname,
					ClassName: suiteName,
					Time:      result.Duration.Seconds(),
					SystemOut: result.Output,
				}
				if result.Skipped {
					tc.Skipped = &struct{}{}
					totalSkipped++
				} else if result.ExitCode != 0 {
					tc.Failure = &JUnitFailure{
						Message: result.Error.Error(),
						Type:    "TestFailure",
						Content: result.Output,
					}
					totalFailures++
				}
				testSuite.TestCases = append(testSuite.TestCases, tc)
				totalTests++
				testSuite.Time += result.Duration.Seconds()
			}
			testSuite.Tests = totalTests
			testSuite.Failures = totalFailures
			testSuite.Skipped = totalSkipped
			testSuites = append(testSuites, testSuite)
		}
	} else {
		// Aggregate all results into a single test suite.
		singleSuite := JUnitTestSuite{
			Name: suiteName,
			Time: 0.0,
		}
		totalTests := 0
		totalFailures := 0
		totalSkipped := 0

		for _, results := range resultSet {
			for _, result := range results {
				tc := JUnitTestCase{
					Name:      result.JobSpec.Shortname,
					ClassName: suiteName,
					Time:      result.Duration.Seconds(),
					SystemOut: result.Output,
				}
				if result.Skipped {
					tc.Skipped = &struct{}{}
					totalSkipped++
				} else if result.ExitCode != 0 {
					tc.Failure = &JUnitFailure{
						Message: result.Error.Error(),
						Type:    "TestFailure",
						Content: result.Output,
					}
					totalFailures++
				}
				singleSuite.TestCases = append(singleSuite.TestCases, tc)
				totalTests++
				singleSuite.Time += result.Duration.Seconds()
			}
		}
		singleSuite.Tests = totalTests
		singleSuite.Failures = totalFailures
		singleSuite.Skipped = totalSkipped
		testSuites = append(testSuites, singleSuite)
	}

	report := JUnitReport{
		TestSuites: testSuites,
	}

	output, err := xml.MarshalIndent(report, "", "  ")
	if err != nil {
		return fmt.Errorf("error marshalling JUnit XML: %w", err)
	}

	reportDir := filepath.Dir(filename)
	if err := os.MkdirAll(reportDir, 0755); err != nil {
		return fmt.Errorf("failed to create report directory %s: %w", reportDir, err)
	}

	err = ioutil.WriteFile(filename, []byte(xml.Header+string(output)), 0644)
	if err != nil {
		return fmt.Errorf("failed to write JUnit XML report to %s: %w", filename, err)
	}
	return nil
}

// --- Language Interface and Registry ---

// LanguageInterface defines the common interface for all language test runners.
type LanguageInterface interface {
	Configure(config *Config, args *Args)
	TestSpecs() ([]JobSpec, error)
	PreBuildSteps() ([]JobSpec, error)
	BuildSteps() ([]JobSpec, error)
	BuildStepsEnviron() (map[string]string, error)
	PostTestsSteps() ([]JobSpec, error)
	DockerfileDir() (string, error) // Returns Dockerfile directory or error if not applicable
	String() string                 // Returns language name
}

// RegisterLanguage is called by init functions of each language package (now within the single file).
func RegisterLanguage(name string, lang LanguageInterface) {
	if _, exists := languageRegistry[name]; exists {
		panic(fmt.Sprintf("language %s already registered", name))
	}
	languageRegistry[name] = lang
}

// GetLanguage retrieves a language implementation from the registry.
func GetLanguage(name string) (LanguageInterface, error) {
	lang, ok := languageRegistry[name]
	if !ok {
		return nil, fmt.Errorf("unsupported language: %s", name)
	}
	return lang, nil
}

// --- Language Implementations (C, CSharp, ObjC, Php8, Python, Ruby, Sanity) ---

// C Language Implementation
type CLanguage struct {
	langSuffix                 string
	platform                   string
	testLang                   string
	config                     *Config
	args                       *Args
	cmakeConfigureExtraArgs    []string
	cmakeGeneratorWindows      string
	cmakeArchitectureWindows   string
	activateVSToolsWindows     string
	vsToolsArchitectureWindows string
	dockerDistro               string
}

func init() {
	RegisterLanguage("c", &CLanguage{langSuffix: "c", testLang: "c"})
	RegisterLanguage("c++", &CLanguage{langSuffix: "cxx", testLang: "c++"})
}

func (l *CLanguage) Configure(cfg *Config, args *Args) {
	l.config = cfg
	l.args = args
	l.platform = platformString()

	if l.platform == "windows" {
		if err := checkCompiler(l.args.Compiler, []string{"default", "cmake", "cmake_ninja_vs2022", "cmake_vs2022"}); err != nil {
			log.Fatalf("Compiler check failed for C language on Windows: %v", err)
		}
		if err := checkArch(l.args.Arch, []string{"default", "x64", "x86"}); err != nil {
			log.Fatalf("Architecture check failed for C language on Windows: %v", err)
		}

		if l.args.Compiler == "cmake_ninja_vs2022" || l.args.Compiler == "cmake" || l.args.Compiler == "default" {
			l.cmakeGeneratorWindows = "Ninja"
			l.activateVSToolsWindows = "2022"
		} else if l.args.Compiler == "cmake_vs2022" {
			l.cmakeGeneratorWindows = "Visual Studio 17 2022"
		} else {
			log.Fatalf("Unexpected compiler for C language on Windows: %s", l.args.Compiler)
		}

		l.cmakeConfigureExtraArgs = append([]string{}, l.args.CMakeConfigureExtraArgs...)
		l.cmakeConfigureExtraArgs = append(l.cmakeConfigureExtraArgs, "-DCMAKE_CXX_STANDARD=17")
		l.cmakeArchitectureWindows = "x64"
		if l.args.Arch == "x86" {
			l.cmakeArchitectureWindows = "Win32"
		}
		l.vsToolsArchitectureWindows = "x64"
		if l.args.Arch == "x86" {
			l.vsToolsArchitectureWindows = "x64_x86"
		}
	} else {
		if l.platform == "linux" {
			if err := checkArch(l.args.Arch, []string{"default", "x64", "x86", "arm64"}); err != nil {
				log.Fatalf("Architecture check failed for C language on Linux: %v", err)
			}
		} else {
			if err := checkArch(l.args.Arch, []string{"default"}); err != nil {
				log.Fatalf("Architecture check failed for C language on non-Linux/Windows: %v", err)
			}
		}
		l.dockerDistro, l.cmakeConfigureExtraArgs = l.compilerOptions(l.args.UseDocker, l.args.Compiler, l.args.CMakeConfigureExtraArgs)
	}
}

func (l *CLanguage) TestSpecs() ([]JobSpec, error) {
	var out []JobSpec
	binaries, err := getCTests(l.args.Travis, l.testLang)
	if err != nil {
		return nil, fmt.Errorf("failed to get C tests: %w", err)
	}

	for _, targetMap := range binaries {
		// Convert map[string]interface{} to a strongly typed struct if needed, or access dynamically
		// For now, dynamic access mirroring Python's dict.get()
		get := func(key string, defaultValue interface{}) interface{} {
			if val, ok := targetMap[key]; ok {
				return val
			}
			return defaultValue
		}

		if get("boringssl", false).(bool) {
			continue
		}

		autoTimeoutScaling := get("auto_timeout_scaling", true).(bool)
		pollingStrs := []string{"all"}
		if get("uses_polling", true).(bool) {
			if strategies, ok := pollingStrategies[l.platform]; ok {
				pollingStrs = strategies
			}
		} else {
			pollingStrs = []string{"none"}
		}

		for _, pollingStrategy := range pollingStrs {
			env := map[string]string{
				"GRPC_DEFAULT_SSL_ROOTS_FILE_PATH": filepath.Join(os.Getenv("GRPC_ROOT"), "src", "core", "tsi", "test_creds", "ca.pem"),
				"GRPC_POLL_STRATEGY":               pollingStrategy,
				"GRPC_VERBOSITY":                   "DEBUG",
			}
			if resolver := os.Getenv("GRPC_DNS_RESOLVER"); resolver != "" {
				env["GRPC_DNS_RESOLVER"] = resolver
			}
			shortnameExt := ""
			if pollingStrategy != "all" {
				shortnameExt = fmt.Sprintf(" GRPC_POLL_STRATEGY=%s", pollingStrategy)
			}
			if excludedPollEngines, ok := get("excluded_poll_engines", nil).([]interface{}); ok {
				if stringInSlice(pollingStrategy, excludedPollEngines) {
					continue
				}
			}

			timeoutScaling := 1.0
			if autoTimeoutScaling {
				config := l.args.Config
				if strings.Contains(config, "asan") || config == "msan" || config == "tsan" || config == "ubsan" || config == "helgrind" || config == "memcheck" {
					timeoutScaling *= 3
				}
			}

			if excludeConfigs, ok := get("exclude_configs", nil).([]interface{}); ok {
				if stringInSlice(l.config.BuildConfig, excludeConfigs) {
					continue
				}
			}
			if excludeIOMgrs, ok := get("exclude_iomgrs", nil).([]interface{}); ok {
				if stringInSlice(l.args.IOMgrPlatform, excludeIOMgrs) {
					continue
				}
			}

			var binary string
			targetName := get("name", "").(string)
			if l.platform == "windows" {
				if l.cmakeGeneratorWindows == "Ninja" {
					binary = filepath.Join("cmake", "build", targetName+".exe")
				} else {
					binary = filepath.Join("cmake", "build", msbuildConfig[l.config.BuildConfig], targetName+".exe")
				}
			} else {
				binary = filepath.Join("cmake", "build", targetName)
			}

			cpuCost := 1.0 // Default
			if rawCPUCost, ok := get("cpu_cost", 1.0).(float64); ok {
				cpuCost = rawCPUCost
			} else if rawCPUCostStr, ok := get("cpu_cost", nil).(string); ok && rawCPUCostStr == "capacity" {
				cpuCost = float64(runtime.NumCPU())
			}

			if _, err := os.Stat(binary); os.IsNotExist(err) {
				if l.args.Regex == ".*" || l.platform == "windows" {
					log.Printf("WARNING: binary not found, skipping %s\n", binary)
				}
				continue
			}

			targetArgsRaw := get("args", []interface{}(nil)).([]interface{})
			targetArgs := convertInterfaceSliceToStringSlice(targetArgsRaw)

			baseTimeout := get("timeout_seconds", float64(defaultTimeoutSeconds/time.Second)).(float64)

			if get("benchmark", false).(bool) {
				cmd := exec.Command(binary, "--benchmark_list_tests")
				output, err := cmd.CombinedOutput()
				if err != nil {
					return nil, fmt.Errorf("failed to list benchmark tests for %s: %w", binary, err)
				}
				for _, line := range strings.Split(string(output), "\n") {
					test := strings.TrimSpace(line)
					if test == "" {
						continue
					}
					cmdline := []string{binary, fmt.Sprintf("--benchmark_filter=%s$", test)}
					cmdline = append(cmdline, targetArgs...)
					out = append(out, l.config.JobSpec(
						cmdline,
						Shortname(fmt.Sprintf("%s %s", strings.Join(cmdline, " "), shortnameExt)),
						CPUCost(cpuCost),
						TimeoutSeconds(baseTimeout*timeoutScaling),
						Environ(env),
					))
				}
			} else if get("gtest", false).(bool) {
				cmd := exec.Command(binary, "--gtest_list_tests")
				output, err := cmd.CombinedOutput()
				if err != nil {
					return nil, fmt.Errorf("failed to list gtests for %s: %w", binary, err)
				}
				base := ""
				for _, line := range strings.Split(string(output), "\n") {
					line = strings.TrimSpace(line)
					if strings.HasSuffix(line, ".") {
						base = strings.TrimSuffix(line, ".")
					} else if base != "" && strings.HasPrefix(line, " ") {
						test := fmt.Sprintf("%s.%s", base, strings.TrimSpace(line))
						cmdline := []string{binary, fmt.Sprintf("--gtest_filter=%s", test)}
						cmdline = append(cmdline, targetArgs...)
						out = append(out, l.config.JobSpec(
							cmdline,
							Shortname(fmt.Sprintf("%s %s", strings.Join(cmdline, " "), shortnameExt)),
							CPUCost(cpuCost),
							TimeoutSeconds(baseTimeout*timeoutScaling),
							Environ(env),
						))
					}
				}
			} else {
				cmdline := []string{binary}
				cmdline = append(cmdline, targetArgs...)
				shortname := get("shortname", strings.Join(quoteArgs(cmdline), " ")).(string)
				shortname += shortnameExt
				out = append(out, l.config.JobSpec(
					cmdline,
					Shortname(shortname),
					CPUCost(cpuCost),
					Flaky(get("flaky", false).(bool)),
					TimeoutSeconds(baseTimeout*timeoutScaling),
					Environ(env),
				))
			}
		}
	}

	// Sort jobs as in Python
	sort.Slice(out, func(i, j int) bool {
		return out[i].Shortname < out[j].Shortname
	})

	return out, nil
}

func (l *CLanguage) PreBuildSteps() ([]JobSpec, error) {
	return []JobSpec{}, nil
}

func (l *CLanguage) BuildSteps() ([]JobSpec, error) {
	var cmdline []string
	if l.platform == "windows" {
		cmdline = []string{
			"tools\\run_tests\\helper_scripts\\build_cxx.bat",
			fmt.Sprintf("-DgRPC_BUILD_MSVC_MP_COUNT=%d", l.args.Jobs),
		}
		cmdline = append(cmdline, l.cmakeConfigureExtraArgs...)
	} else {
		cmdline = []string{"tools/run_tests/helper_scripts/build_cxx.sh"}
		cmdline = append(cmdline, l.cmakeConfigureExtraArgs...)
	}
	return []JobSpec{l.config.JobSpec(cmdline)}, nil
}

func (l *CLanguage) BuildStepsEnviron() (map[string]string, error) {
	environ := map[string]string{"GRPC_RUN_TESTS_CXX_LANGUAGE_SUFFIX": l.langSuffix}
	if l.platform == "windows" {
		environ["GRPC_CMAKE_GENERATOR"] = l.cmakeGeneratorWindows
		environ["GRPC_CMAKE_ARCHITECTURE"] = l.cmakeArchitectureWindows
		environ["GRPC_BUILD_ACTIVATE_VS_TOOLS"] = l.activateVSToolsWindows
		environ["GRPC_BUILD_VS_TOOLS_ARCHITECTURE"] = l.vsToolsArchitectureWindows
	} else if l.platform == "linux" {
		environ["GRPC_RUNTESTS_ARCHITECTURE"] = l.args.Arch
	}
	// Add GRPC_RUN_TESTS_JOBS as in Python's _build_step_environ
	environ["GRPC_RUN_TESTS_JOBS"] = strconv.Itoa(l.args.Jobs)
	return environ, nil
}

func (l *CLanguage) PostTestsSteps() ([]JobSpec, error) {
	if l.platform == "windows" {
		return []JobSpec{}, nil
	} else {
		return []JobSpec{l.config.JobSpec([]string{"tools/run_tests/helper_scripts/post_tests_c.sh"})}, nil
	}
}

func (l *CLanguage) DockerfileDir() (string, error) {
	if l.dockerDistro == "" {
		return "", fmt.Errorf("docker distro not set for CLanguage, cannot determine DockerfileDir")
	}
	return filepath.Join("tools", "dockerfile", "test", fmt.Sprintf("cxx_%s_%s", l.dockerDistro, dockerArchSuffix(l.args.Arch))), nil
}

func (l *CLanguage) String() string {
	return l.langSuffix
}

func (l *CLanguage) clangCMakeConfigureExtraArgs(versionSuffix string) []string {
	return []string{
		fmt.Sprintf("-DCMAKE_C_COMPILER=clang%s", versionSuffix),
		fmt.Sprintf("-DCMAKE_CXX_COMPILER=clang++%s", versionSuffix),
	}
}

func (l *CLanguage) compilerOptions(useDocker bool, compiler string, cmakeConfigureExtraArgs []string) (string, []string) {
	if len(cmakeConfigureExtraArgs) > 0 {
		if err := checkCompiler(compiler, []string{"default", "cmake"}); err != nil {
			log.Fatalf("Compiler check failed: %v", err)
		}
		return "nonexistent_docker_distro", cmakeConfigureExtraArgs
	}
	if !useDocker && !isUseDockerChild() {
		if err := checkCompiler(compiler, []string{"default", "cmake"}); err != nil {
			log.Fatalf("Compiler check failed when not using docker: %v", err)
		}
	}

	switch compiler {
	case "default", "cmake":
		return "debian11", []string{"-DCMAKE_CXX_STANDARD=17"}
	case "gcc8":
		return "gcc_8", []string{"-DCMAKE_CXX_STANDARD=17"}
	case "gcc10.2":
		return "debian11", []string{"-DCMAKE_CXX_STANDARD=17"}
	case "gcc10.2_openssl102":
		return "debian11_openssl102", []string{"-DgRPC_SSL_PROVIDER=package", "-DCMAKE_CXX_STANDARD=17"}
	case "gcc10.2_openssl111":
		return "debian11_openssl111", []string{"-DgRPC_SSL_PROVIDER=package", "-DCMAKE_CXX_STANDARD=17"}
	case "gcc12_openssl309":
		return "debian12_openssl309", []string{"-DgRPC_SSL_PROVIDER=package", "-DCMAKE_CXX_STANDARD=17"}
	case "gcc14":
		return "gcc_14", []string{"-DCMAKE_CXX_STANDARD=20"}
	case "gcc_musl":
		return "alpine", []string{"-DCMAKE_CXX_STANDARD=17"}
	case "clang11":
		return "clang_11", append(l.clangCMakeConfigureExtraArgs(""), "-DCMAKE_CXX_STANDARD=17")
	case "clang19":
		return "clang_19", append(l.clangCMakeConfigureExtraArgs(""), "-DCMAKE_CXX_STANDARD=17")
	default:
		log.Fatalf("Compiler %s not supported for C/C++ language.", compiler)
		return "", nil // Should not be reached
	}
}

// Php8 Language Implementation
type Php8Language struct {
	config *Config
	args   *Args
}

func init() {
	RegisterLanguage("php8", &Php8Language{})
}

func (l *Php8Language) Configure(cfg *Config, args *Args) {
	l.config = cfg
	l.args = args
	if err := checkCompiler(l.args.Compiler, []string{"default"}); err != nil {
		log.Fatalf("Compiler check failed for PHP8: %v", err)
	}
}

func (l *Php8Language) TestSpecs() ([]JobSpec, error) {
	return []JobSpec{
		l.config.JobSpec(
			[]string{"src/php/bin/run_tests.sh"},
			Environ(forceEnvironForWrappers),
		),
	}, nil
}

func (l *Php8Language) PreBuildSteps() ([]JobSpec, error) { return []JobSpec{}, nil }
func (l *Php8Language) BuildSteps() ([]JobSpec, error) {
	return []JobSpec{l.config.JobSpec([]string{"tools/run_tests/helper_scripts/build_php.sh"})}, nil
}
func (l *Php8Language) BuildStepsEnviron() (map[string]string, error) {
	return map[string]string{"GRPC_RUN_TESTS_JOBS": strconv.Itoa(l.args.Jobs)}, nil
}
func (l *Php8Language) PostTestsSteps() ([]JobSpec, error) {
	return []JobSpec{l.config.JobSpec([]string{"tools/run_tests/helper_scripts/post_tests_php.sh"})}, nil
}
func (l *Php8Language) DockerfileDir() (string, error) {
	return filepath.Join("tools", "dockerfile", "test", fmt.Sprintf("php8_debian12_%s", dockerArchSuffix(l.args.Arch))), nil
}
func (l *Php8Language) String() string { return "php8" }

// Python Language Implementation
type PythonConfigTuple struct {
	Name       string
	BuildCmd   []string
	RunCmd     []string
	PythonPath string
}

type PythonLanguage struct {
	config  *Config
	args    *Args
	pythons []PythonConfigTuple
}

func init() {
	RegisterLanguage("python", &PythonLanguage{})
}

func (l *PythonLanguage) Configure(cfg *Config, args *Args) {
	l.config = cfg
	l.args = args
	l.pythons = l.getPythons(l.args)
}

func (l *PythonLanguage) TestSpecs() ([]JobSpec, error) {
	jobs := []JobSpec{}

	testSpecsFile := map[string][]string{
		"native":  {"src/python/grpcio_tests/tests/tests.json"},
		"asyncio": {"src/python/grpcio_tests/tests_aio/tests.json"},
	}

	testCommand := map[string]string{
		"native":  "test_lite",
		"asyncio": "test_aio",
	}

	for _, pythonConfig := range l.pythons {
		if platformString() != "windows" { // os.name != "nt" equivalent
			jobs = append(jobs, l.config.JobSpec(
				[]string{pythonConfig.PythonPath, "tools/distrib/python/xds_protos/generated_file_import_test.py"},
				Shortname(fmt.Sprintf("%s.xds_protos", pythonConfig.Name)),
				TimeoutSeconds(60),
				Environ(forceEnvironForWrappers),
			))
		}

		for ioPlatform, testsJSONFiles := range testSpecsFile {
			var testCases []string
			for _, filename := range testsJSONFiles {
				data, err := ioutil.ReadFile(filepath.Join(os.Getenv("GRPC_ROOT"), filename))
				if err != nil {
					return nil, fmt.Errorf("failed to read Python test JSON file %s: %w", filename, err)
				}
				var cases []string
				if err := json.Unmarshal(data, &cases); err != nil {
					return nil, fmt.Errorf("failed to unmarshal Python test JSON from %s: %w", filename, err)
				}
				testCases = append(testCases, cases...)
			}

			environment := make(map[string]string)
			for k, v := range forceEnvironForWrappers { // Copy base environment
				environment[k] = v
			}
			if ioPlatform != "native" {
				environment["GRPC_ENABLE_FORK_SUPPORT"] = "0"
			}

			for _, testCase := range testCases {
				envForTest := make(map[string]string)
				for k, v := range environment {
					envForTest[k] = v
				}
				envForTest["GRPC_PYTHON_TESTRUNNER_FILTER"] = testCase

				jobs = append(jobs, l.config.JobSpec(
					append(pythonConfig.RunCmd, testCommand[ioPlatform]),
					Shortname(fmt.Sprintf("%s.%s.%s", pythonConfig.Name, ioPlatform, testCase)),
					TimeoutSeconds(10*60),
					Environ(envForTest),
				))
			}
		}
	}
	return jobs, nil
}

func (l *PythonLanguage) PreBuildSteps() ([]JobSpec, error) { return []JobSpec{}, nil }
func (l *PythonLanguage) BuildSteps() ([]JobSpec, error) {
	var specs []JobSpec
	for _, pyCfg := range l.pythons {
		specs = append(specs, l.config.JobSpec(pyCfg.BuildCmd))
	}
	return specs, nil
}
func (l *PythonLanguage) BuildStepsEnviron() (map[string]string, error) {
	return map[string]string{"GRPC_RUN_TESTS_JOBS": strconv.Itoa(l.args.Jobs)}, nil
}
func (l *PythonLanguage) PostTestsSteps() ([]JobSpec, error) {
	if l.config.BuildConfig != "gcov" {
		return []JobSpec{}, nil
	}
	return []JobSpec{l.config.JobSpec([]string{"tools/run_tests/helper_scripts/post_tests_python.sh"})}, nil
}
func (l *PythonLanguage) DockerfileDir() (string, error) {
	return filepath.Join("tools", "dockerfile", "test", fmt.Sprintf("python_%s_%s", l.pythonDockerDistroName(), dockerArchSuffix(l.args.Arch))), nil
}
func (l *PythonLanguage) String() string { return "python" }

func (l *PythonLanguage) pythonDockerDistroName() string {
	if l.args.Compiler == "python_alpine" {
		return "alpine"
	}
	return "debian11_default"
}

func (l *PythonLanguage) getPythons(args *Args) []PythonConfigTuple {
	if args.IOMgrPlatform != "native" {
		log.Fatalf("Python builds no longer differentiate IO Manager platforms, please use \"native\"")
	}

	bits := "64"
	if args.Arch == "x86" {
		bits = "32"
	}

	var (
		shellCmd                  []string
		builderCmd                []string
		builderPrefixArgumentsCmd []string
		venvRelativePythonCmd     []string
		toolchainCmd              []string
		runnerCmd                 []string
	)

	if runtime.GOOS == "windows" {
		shellCmd = []string{"bash"}
		builderCmd = []string{filepath.Join(os.Getenv("GRPC_ROOT"), "tools", "run_tests", "helper_scripts", "build_python_msys2.sh")}
		builderPrefixArgumentsCmd = []string{fmt.Sprintf("MINGW%s", bits)}
		venvRelativePythonCmd = []string{"Scripts/python.exe"}
		toolchainCmd = []string{"mingw32"}
	} else {
		shellCmd = []string{}
		builderCmd = []string{filepath.Join(os.Getenv("GRPC_ROOT"), "tools", "run_tests", "helper_scripts", "build_python.sh")}
		builderPrefixArgumentsCmd = []string{}
		venvRelativePythonCmd = []string{"bin/python"}
		toolchainCmd = []string{"unix"}
	}
	runnerCmd = []string{filepath.Join(os.Getenv("GRPC_ROOT"), "tools", "run_tests", "helper_scripts", "run_python.sh")}

	pythonPatternFunction := func(major, minor, bits string) string {
		if runtime.GOOS == "windows" {
			if bits == "64" {
				return fmt.Sprintf("/c/Python%s%s/python.exe", major, minor)
			}
			return fmt.Sprintf("/c/Python%s%s_%sbits/python.exe", major, minor, bits)
		}
		return fmt.Sprintf("python%s.%s", major, minor)
	}

	pypyPatternFunction := func(major string) string {
		if major == "2" {
			return "pypy"
		} else if major == "3" {
			return "pypy3"
		}
		log.Fatalf("Unknown PyPy major version: %s", major)
		return ""
	}

	pythonConfigGenerator := func(name, major, minor, bits string) PythonConfigTuple {
		build := append(shellCmd, builderCmd...)
		build = append(build, builderPrefixArgumentsCmd...)
		build = append(build, pythonPatternFunction(major, minor, bits))
		build = append(build, name)
		build = append(build, venvRelativePythonCmd...)
		build = append(build, toolchainCmd...)
		pythonPath := filepath.Join(name, venvRelativePythonCmd[0])
		run := append(shellCmd, runnerCmd...)
		run = append(run, pythonPath)
		return PythonConfigTuple{name, build, run, pythonPath}
	}

	pypyConfigGenerator := func(name, major string) PythonConfigTuple {
		build := append(shellCmd, builderCmd...)
		build = append(build, builderPrefixArgumentsCmd...)
		build = append(build, pypyPatternFunction(major))
		build = append(build, name)
		build = append(build, venvRelativePythonCmd...)
		build = append(build, toolchainCmd...)
		pythonPath := filepath.Join(name, venvRelativePythonCmd[0])
		run := append(shellCmd, runnerCmd...)
		run = append(run, pythonPath)
		return PythonConfigTuple{name, build, run, pythonPath}
	}

	python39Config := pythonConfigGenerator("py39", "3", "9", bits)
	python310Config := pythonConfigGenerator("py310", "3", "10", bits)
	python311Config := pythonConfigGenerator("py311", "3", "11", bits)
	python312Config := pythonConfigGenerator("py312", "3", "12", bits)
	python313Config := pythonConfigGenerator("py313", "3", "13", bits)
	pypy27Config := pypyConfigGenerator("pypy", "2")
	pypy32Config := pypyConfigGenerator("pypy3", "3")

	switch args.Compiler {
	case "default":
		if runtime.GOOS == "windows" {
			return []PythonConfigTuple{python39Config}
		} else if runtime.GOOS == "darwin" { // os.uname()[0] == "Darwin" equivalent
			return []PythonConfigTuple{python39Config}
		} else if runtime.GOARCH == "arm64" { // platform.machine() == "aarch64" equivalent
			return []PythonConfigTuple{python39Config}
		} else {
			return []PythonConfigTuple{python39Config, python313Config}
		}
	case "python3.9":
		return []PythonConfigTuple{python39Config}
	case "python3.10":
		return []PythonConfigTuple{python310Config}
	case "python3.11":
		return []PythonConfigTuple{python311Config}
	case "python3.12":
		return []PythonConfigTuple{python312Config}
	case "python3.13":
		return []PythonConfigTuple{python313Config}
	case "pypy":
		return []PythonConfigTuple{pypy27Config}
	case "pypy3":
		return []PythonConfigTuple{pypy32Config}
	case "python_alpine":
		return []PythonConfigTuple{python311Config}
	case "all_the_cpythons":
		return []PythonConfigTuple{python39Config, python310Config, python311Config, python312Config, python313Config}
	default:
		log.Fatalf("Compiler %s not supported for Python language.", args.Compiler)
		return nil // Should not be reached
	}
}

// Ruby Language Implementation
type RubyLanguage struct {
	config *Config
	args   *Args
}

func init() {
	RegisterLanguage("ruby", &RubyLanguage{})
}

func (l *RubyLanguage) Configure(cfg *Config, args *Args) {
	l.config = cfg
	l.args = args
	if err := checkCompiler(l.args.Compiler, []string{"default"}); err != nil {
		log.Fatalf("Compiler check failed for Ruby: %v", err)
	}
}

func (l *RubyLanguage) TestSpecs() ([]JobSpec, error) {
	var tests []JobSpec
	specTests := []string{
		"src/ruby/spec/google_rpc_status_utils_spec.rb",
		"src/ruby/spec/client_server_spec.rb",
		"src/ruby/spec/errors_spec.rb",
		"src/ruby/spec/pb/codegen/package_option_spec.rb",
		"src/ruby/spec/pb/health/checker_spec.rb",
		"src/ruby/spec/pb/duplicate/codegen_spec.rb",
		"src/ruby/spec/server_spec.rb",
		"src/ruby/spec/error_sanity_spec.rb",
		"src/ruby/spec/channel_spec.rb",
		"src/ruby/spec/user_agent_spec.rb",
		"src/ruby/spec/call_credentials_spec.rb",
		"src/ruby/spec/channel_credentials_spec.rb",
		"src/ruby/spec/channel_connection_spec.rb",
		"src/ruby/spec/compression_options_spec.rb",
		"src/ruby/spec/time_consts_spec.rb",
		"src/ruby/spec/server_credentials_spec.rb",
		"src/ruby/spec/generic/server_interceptors_spec.rb",
		"src/ruby/spec/generic/rpc_server_pool_spec.rb",
		"src/ruby/spec/generic/client_stub_spec.rb",
		"src/ruby/spec/generic/active_call_spec.rb",
		"src/ruby/spec/generic/rpc_server_spec.rb",
		"src/ruby/spec/generic/service_spec.rb",
		"src/ruby/spec/generic/client_interceptors_spec.rb",
		"src/ruby/spec/generic/rpc_desc_spec.rb",
		"src/ruby/spec/generic/interceptor_registry_spec.rb",
		"src/ruby/spec/debug_message_spec.rb",
		"src/ruby/spec/logconfig_spec.rb",
		"src/ruby/spec/call_spec.rb",
		"src/ruby/spec/client_auth_spec.rb",
	}
	for _, test := range specTests {
		tests = append(tests, l.config.JobSpec(
			[]string{"bundle", "exec", "rspec", test},
			Shortname(test),
			TimeoutSeconds(20*60),
			Environ(forceEnvironForWrappers),
		))
	}

	end2endTests := []string{
		"src/ruby/end2end/fork_test.rb",
		"src/ruby/end2end/connectivity_watch_interrupted_test.rb",
		"src/ruby/end2end/simple_fork_test.rb",
		"src/ruby/end2end/prefork_without_using_grpc_test.rb",
		"src/ruby/end2end/prefork_postfork_loop_test.rb",
		"src/ruby/end2end/secure_fork_test.rb",
		"src/ruby/end2end/bad_usage_fork_test.rb",
		"src/ruby/end2end/sig_handling_test.rb",
		"src/ruby/end2end/channel_closing_test.rb",
		"src/ruby/end2end/killed_client_thread_test.rb",
		"src/ruby/end2end/forking_client_test.rb",
		"src/ruby/end2end/fork_test_repro_35489.rb",
		"src/ruby/end2end/multiple_killed_watching_threads_test.rb",
		"src/ruby/end2end/client_memory_usage_test.rb",
		"src/ruby/end2end/package_with_underscore_test.rb",
		"src/ruby/end2end/graceful_sig_handling_test.rb",
		"src/ruby/end2end/graceful_sig_stop_test.rb",
		"src/ruby/end2end/errors_load_before_grpc_lib_test.rb",
		"src/ruby/end2end/logger_load_before_grpc_lib_test.rb",
		"src/ruby/end2end/status_codes_load_before_grpc_lib_test.rb",
		"src/ruby/end2end/shell_out_from_server_test.rb",
		"src/ruby/end2end/call_credentials_timeout_test.rb",
		"src/ruby/end2end/call_credentials_returning_bad_metadata_doesnt_kill_background_thread_test.rb",
	}

	forkTests := map[string]bool{
		"src/ruby/end2end/fork_test.rb":                       true,
		"src/ruby/end2end/simple_fork_test.rb":                true,
		"src/ruby/end2end/secure_fork_test.rb":                true,
		"src/ruby/end2end/bad_usage_fork_test.rb":             true,
		"src/ruby/end2end/prefork_without_using_grpc_test.rb": true,
		"src/ruby/end2end/prefork_postfork_loop_test.rb":      true,
		"src/ruby/end2end/fork_test_repro_35489.rb":           true,
	}

	for _, test := range end2endTests {
		if forkTests[test] && platformString() == "mac" {
			continue // Fork support only present on linux
		}
		tests = append(tests, l.config.JobSpec(
			[]string{"ruby", test},
			Shortname(test),
			TimeoutSeconds(20*60),
			Environ(forceEnvironForWrappers),
		))
	}
	return tests, nil
}

func (l *RubyLanguage) PreBuildSteps() ([]JobSpec, error) {
	return []JobSpec{l.config.JobSpec([]string{"tools/run_tests/helper_scripts/pre_build_ruby.sh"})}, nil
}
func (l *RubyLanguage) BuildSteps() ([]JobSpec, error) {
	return []JobSpec{l.config.JobSpec([]string{"tools/run_tests/helper_scripts/build_ruby.sh"})}, nil
}
func (l *RubyLanguage) BuildStepsEnviron() (map[string]string, error) {
	return map[string]string{"GRPC_RUN_TESTS_JOBS": strconv.Itoa(l.args.Jobs)}, nil
}
func (l *RubyLanguage) PostTestsSteps() ([]JobSpec, error) {
	return []JobSpec{l.config.JobSpec([]string{"tools/run_tests/helper_scripts/post_tests_ruby.sh"})}, nil
}
func (l *RubyLanguage) DockerfileDir() (string, error) {
	return filepath.Join("tools", "dockerfile", "test", fmt.Sprintf("ruby_debian11_%s", dockerArchSuffix(l.args.Arch))), nil
}
func (l *RubyLanguage) String() string { return "ruby" }

// CSharp Language Implementation
type CSharpLanguage struct {
	platform        string
	config          *Config
	args            *Args
	testRuntimes    []string
	cmakeArchOption string // Corresponds to _cmake_arch_option
	dockerDistro    string // Corresponds to _docker_distro
}

func init() {
	RegisterLanguage("csharp", &CSharpLanguage{platform: platformString()})
}

func (l *CSharpLanguage) Configure(cfg *Config, args *Args) {
	l.config = cfg
	l.args = args
	if err := checkCompiler(l.args.Compiler, []string{"default", "coreclr", "mono"}); err != nil {
		log.Fatalf("Compiler check failed for CSharp: %v", err)
	}

	if l.args.Compiler == "default" {
		l.testRuntimes = []string{"coreclr", "mono"}
	} else {
		l.testRuntimes = []string{l.args.Compiler}
	}

	if l.platform == "windows" {
		if err := checkArch(l.args.Arch, []string{"default"}); err != nil {
			log.Fatalf("Architecture check failed for CSharp on Windows: %v", err)
		}
		l.cmakeArchOption = "x64"
	} else {
		l.dockerDistro = "debian11"
	}
}

func (l *CSharpLanguage) TestSpecs() ([]JobSpec, error) {
	testsJSONPath := filepath.Join(os.Getenv("GRPC_ROOT"), "src", "csharp", "tests.json")
	data, err := ioutil.ReadFile(testsJSONPath)
	if err != nil {
		return nil, fmt.Errorf("failed to read CSharp tests.json: %w", err)
	}

	var testsByAssembly map[string][]string
	if err := json.Unmarshal(data, &testsByAssembly); err != nil {
		return nil, fmt.Errorf("failed to unmarshal CSharp tests.json: %w", err)
	}

	nunitArgs := []string{"--labels=All", "--noresult", "--workers=1"}
	var specs []JobSpec

	for _, testRuntime := range l.testRuntimes {
		var assemblyExtension string
		var assemblySubdir string
		var runtimeCmd []string

		switch testRuntime {
		case "coreclr":
			assemblyExtension = ".dll"
			assemblySubdir = filepath.Join("bin", msbuildConfig[l.config.BuildConfig], "netcoreapp3.1")
			runtimeCmd = []string{"dotnet", "exec"}
		case "mono":
			assemblyExtension = ".exe"
			assemblySubdir = filepath.Join("bin", msbuildConfig[l.config.BuildConfig], "net45")
			if l.platform == "windows" {
				runtimeCmd = []string{} // No explicit runtime command needed, executable is directly runnable
			} else if l.platform == "mac" {
				runtimeCmd = []string{"mono", "--arch=64"}
			} else {
				runtimeCmd = []string{"mono"}
			}
		default:
			return nil, fmt.Errorf("illegal CSharp runtime \"%s\" was specified", testRuntime)
		}

		for assembly, testsList := range testsByAssembly {
			assemblyFile := filepath.Join("src", "csharp", assembly, assemblySubdir, assembly+assemblyExtension)

			for _, test := range testsList {
				cmdline := append(runtimeCmd, assemblyFile, fmt.Sprintf("--test=%s", test))
				cmdline = append(cmdline, nunitArgs...)
				specs = append(specs, l.config.JobSpec(
					cmdline,
					Shortname(fmt.Sprintf("csharp.%s.%s", testRuntime, test)),
					Environ(forceEnvironForWrappers),
				))
			}
		}
	}
	return specs, nil
}

func (l *CSharpLanguage) PreBuildSteps() ([]JobSpec, error) {
	if l.platform == "windows" {
		return []JobSpec{l.config.JobSpec([]string{"tools\\run_tests\\helper_scripts\\pre_build_csharp.bat"})}, nil
	}
	return []JobSpec{l.config.JobSpec([]string{"tools/run_tests/helper_scripts/pre_build_csharp.sh"})}, nil
}
func (l *CSharpLanguage) BuildSteps() ([]JobSpec, error) {
	if l.platform == "windows" {
		return []JobSpec{l.config.JobSpec([]string{"tools\\run_tests\\helper_scripts\\build_csharp.bat"})}, nil
	}
	return []JobSpec{l.config.JobSpec([]string{"tools/run_tests/helper_scripts/build_csharp.sh"})}, nil
}
func (l *CSharpLanguage) BuildStepsEnviron() (map[string]string, error) {
	environ := map[string]string{"GRPC_RUN_TESTS_JOBS": strconv.Itoa(l.args.Jobs)}
	if l.platform == "windows" {
		environ["ARCHITECTURE"] = l.cmakeArchOption
	}
	return environ, nil
}
func (l *CSharpLanguage) PostTestsSteps() ([]JobSpec, error) {
	if l.platform == "windows" {
		return []JobSpec{l.config.JobSpec([]string{"tools\\run_tests\\helper_scripts\\post_tests_csharp.bat"})}, nil
	}
	return []JobSpec{l.config.JobSpec([]string{"tools/run_tests/helper_scripts/post_tests_csharp.sh"})}, nil
}
func (l *CSharpLanguage) DockerfileDir() (string, error) {
	return filepath.Join("tools", "dockerfile", "test", fmt.Sprintf("csharp_%s_%s", l.dockerDistro, dockerArchSuffix(l.args.Arch))), nil
}
func (l *CSharpLanguage) String() string { return "csharp" }

// ObjC Language Implementation
type ObjCLanguage struct {
	config *Config
	args   *Args
}

func init() {
	RegisterLanguage("objc", &ObjCLanguage{})
}

func (l *ObjCLanguage) Configure(cfg *Config, args *Args) {
	l.config = cfg
	l.args = args
	if err := checkCompiler(l.args.Compiler, []string{"default"}); err != nil {
		log.Fatalf("Compiler check failed for ObjC: %v", err)
	}
}

func (l *ObjCLanguage) TestSpecs() ([]JobSpec, error) {
	out := []JobSpec{}
	exampleBase := "src/objective-c/examples"
	testBase := "src/objective-c/tests"

	// Build example tasks
	examples := []struct {
		scheme      string
		examplePath string
		shortname   string
	}{
		{"Sample", filepath.Join(exampleBase, "Sample"), "ios-buildtest-example-sample"},
		{"SwiftSample", filepath.Join(exampleBase, "SwiftSample"), "ios-buildtest-example-switftsample"},
		{"SwiftUseFrameworks", filepath.Join(exampleBase, "SwiftUseFrameworks"), "ios-buildtest-example-switft-use-frameworks"},
		// {"gRPC-Package", ".", "ios-buildtest-example-switft-package"}, // Re-enable after Abseil fixes
	}

	for _, ex := range examples {
		out = append(out, l.config.JobSpec(
			[]string{filepath.Join(testBase, "build_one_example.sh")},
			Shortname(ex.shortname),
			TimeoutSeconds(60*60),
			CPUCost(1e6),
			Environ(map[string]string{"SCHEME": ex.scheme, "EXAMPLE_PATH": ex.examplePath}),
		))
	}

	// watchOS-sample (disabled due to #20258)
	// out.append(l.config.job_spec(
	// 	[]string{'src/objective-c/tests/build_one_example_bazel.sh'},
	// 	shortname='ios-buildtest-example-watchOS-sample',
	// 	cpu_cost=1e6,
	// 	environ={'SCHEME': 'watchOS-sample-WatchKit-App', 'EXAMPLE_PATH': 'src/objective-c/examples/watchOS-sample', 'FRAMEWORKS': 'NO'}))

	// CFStreamTests (disabled due to flakiness and being replaced with event engine)
	// out.append(l.config.job_spec(
	// 	[]string{"test/core/iomgr/ios/CFStreamTests/build_and_run_tests.sh"},
	// 	shortname="ios-test-cfstream-tests",
	// 	cpu_cost=1e6,
	// 	environ=forceEnvironForWrappers,
	// ))

	// Sort jobs as in Python
	sort.Slice(out, func(i, j int) bool {
		return out[i].Shortname < out[j].Shortname
	})

	return out, nil
}

func (l *ObjCLanguage) PreBuildSteps() ([]JobSpec, error) { return []JobSpec{}, nil }
func (l *ObjCLanguage) BuildSteps() ([]JobSpec, error)    { return []JobSpec{}, nil }
func (l *ObjCLanguage) BuildStepsEnviron() (map[string]string, error) {
	return map[string]string{"GRPC_RUN_TESTS_JOBS": strconv.Itoa(l.args.Jobs)}, nil
}
func (l *ObjCLanguage) PostTestsSteps() ([]JobSpec, error) { return []JobSpec{}, nil }
func (l *ObjCLanguage) DockerfileDir() (string, error)     { return "", nil } // Not applicable for ObjC
func (l *ObjCLanguage) String() string                     { return "objc" }

// Sanity Language Implementation
type SanityLanguage struct {
	configFile string
	config     *Config
	args       *Args
}

func init() {
	RegisterLanguage("sanity", &SanityLanguage{configFile: "sanity_tests.yaml"})
	RegisterLanguage("clang-tidy", &SanityLanguage{configFile: "clang_tidy_tests.yaml"})
}

func (l *SanityLanguage) Configure(cfg *Config, args *Args) {
	l.config = cfg
	l.args = args
	if err := checkCompiler(l.args.Compiler, []string{"default"}); err != nil {
		log.Fatalf("Compiler check failed for Sanity: %v", err)
	}
}

func (l *SanityLanguage) TestSpecs() ([]JobSpec, error) {
	type SanityCmd struct {
		Script  string      `yaml:"script"`
		CPUCost interface{} `yaml:"cpu_cost"`
	}

	var sanityCmds []SanityCmd

	yamlFilePath := filepath.Join(os.Getenv("GRPC_ROOT"), "tools", "run_tests", "sanity", l.configFile)
	yamlData, err := ioutil.ReadFile(yamlFilePath) // This is the `yamlData` variable at line ~1983
	if err != nil {
		return nil, fmt.Errorf("failed to read sanity config file %s: %w", yamlFilePath, err)
	}

	if err = yaml.Unmarshal(yamlData, &sanityCmds); err != nil {
		return nil, fmt.Errorf("failed to unmarshal sanity config %s: %w", l.configFile, err)
	}
	// If you do not uncomment the above, the yamlData variable will remain unused,
	// leading to a warning. For now, the dummy data will be used.

	// Dummy data for demonstration if YAML parsing is not enabled
	if len(sanityCmds) == 0 { // Only use dummy data if not unmarshaled from YAML
		if l.configFile == "sanity_tests.yaml" {
			sanityCmds = []SanityCmd{
				{Script: "tools/some_sanity_check.sh", CPUCost: 1},
				{Script: "tools/other_sanity.sh", CPUCost: 2.5},
			}
		} else if l.configFile == "clang_tidy_tests.yaml" {
			sanityCmds = []SanityCmd{
				{Script: "tools/clang_tidy_check.sh", CPUCost: 5},
			}
		}
	}
	// End Dummy data

	environ := map[string]string{"TEST": "true"}
	if isUseDockerChild() {
		environ["CLANG_FORMAT_SKIP_DOCKER"] = "true"
		environ["CLANG_TIDY_SKIP_DOCKER"] = "true"
		environ["IWYU_SKIP_DOCKER"] = "true"
		environ["DISABLE_BAZEL_WRAPPER"] = "true"
	}

	var jobs []JobSpec
	for _, cmd := range sanityCmds {
		cpuCost := 1.0
		if c, ok := cmd.CPUCost.(float64); ok {
			cpuCost = c
		} else if c, ok := cmd.CPUCost.(int); ok { // YAML might unmarshal int for whole numbers
			cpuCost = float64(c)
		}

		jobs = append(jobs, l.config.JobSpec(
			strings.Split(cmd.Script, " "),
			TimeoutSeconds(90*60),
			Environ(environ),
			CPUCost(cpuCost),
		))
	}
	return jobs, nil
}

func (l *SanityLanguage) PreBuildSteps() ([]JobSpec, error) { return []JobSpec{}, nil }
func (l *SanityLanguage) BuildSteps() ([]JobSpec, error)    { return []JobSpec{}, nil }
func (l *SanityLanguage) BuildStepsEnviron() (map[string]string, error) {
	return map[string]string{"GRPC_RUN_TESTS_JOBS": strconv.Itoa(l.args.Jobs)}, nil
}
func (l *SanityLanguage) PostTestsSteps() ([]JobSpec, error) { return []JobSpec{}, nil }
func (l *SanityLanguage) DockerfileDir() (string, error) {
	return filepath.Join("tools", "dockerfile", "test", "sanity"), nil
}
func (l *SanityLanguage) String() string {
	if l.configFile == "clang_tidy_tests.yaml" {
		return "clang-tidy"
	}
	return "sanity"
}

// --- Main Program Logic ---

// initFlags initializes the command-line flags.
func initFlags() {
	flag.StringVar(&parsedArgs.Config, "c", "opt", "Build configuration (opt, dbg, etc.)")
	flag.Var((*runsPerTestType)(&parsedArgs.RunsPerTest), "n", `A positive integer or "inf". If "inf", all tests will run in an infinite loop.`)
	flag.StringVar(&parsedArgs.Regex, "r", ".*", "Regular expression to filter tests by shortname.")
	flag.StringVar(&parsedArgs.RegexExclude, "regex_exclude", "", "Regular expression to exclude tests by shortname.")
	flag.IntVar(&parsedArgs.Jobs, "j", runtime.NumCPU(), "Number of concurrent test jobs.")
	flag.Float64Var(&parsedArgs.Slowdown, "s", 1.0, "Slowdown factor for test timeouts.")
	flag.Var((*percentType)(&parsedArgs.SamplePercent), "p", "Run a random sample with that percentage of tests")
	flag.BoolVar(&parsedArgs.Travis, "t", false, "When set, indicates that the script is running on CI (= not locally).")
	flag.BoolVar(&parsedArgs.NewlineOnSuccess, "newline_on_success", false, "Print newline after each successful job.")
	flag.Var((*stringSliceFlag)(&parsedArgs.Language), "l", "Language(s) to test (e.g., -l c -l python or -l c,python). Required.")
	flag.BoolVar(&parsedArgs.StopOnFailure, "S", false, "Stop immediately on first test failure.")
	flag.BoolVar(&parsedArgs.UseDocker, "use_docker", false, "Run all tests under Docker. Only available on Linux.")
	flag.BoolVar(&parsedArgs.AllowFlakes, "allow_flakes", false, "Allow flaky tests to show as passing (re-runs failed tests up to five times).")
	flag.StringVar(&parsedArgs.Arch, "arch", "default", "Selects architecture to target.")
	flag.StringVar(&parsedArgs.Compiler, "compiler", "default", "Selects compiler to use. Allowed values depend on the platform and language.")
	flag.StringVar(&parsedArgs.IOMgrPlatform, "iomgr_platform", "native", "Selects iomgr platform to build on.")
	flag.BoolVar(&parsedArgs.BuildOnly, "build_only", false, "Perform all the build steps but don't run any tests.")
	flag.BoolVar(&parsedArgs.MeasureCPUCosts, "measure_cpu_costs", false, "Measure the CPU costs of tests.")
	flag.IntVar(&parsedArgs.Antagonists, "a", 0, "Number of antagonist processes to run.")
	flag.StringVar(&parsedArgs.XMLReport, "x", "", "Generates a JUnit-compatible XML report.")
	flag.StringVar(&parsedArgs.ReportSuiteName, "report_suite_name", "tests", "Test suite name to use in generated JUnit XML report.")
	flag.BoolVar(&parsedArgs.ReportMultiTarget, "report_multi_target", false, "Generate separate XML report for each test job (Looks better in UIs).")
	flag.BoolVar(&parsedArgs.QuietSuccess, "quiet_success", false, "Don't print anything when a test passes.")
	flag.BoolVar(&parsedArgs.ForceDefaultPoller, "force_default_poller", false, "Don't try to iterate over many polling strategies when they exist.")
	flag.StringVar(&parsedArgs.ForceUsePollers, "force_use_pollers", "", "Only use the specified comma-delimited list of polling engines.")
	flag.IntVar(&parsedArgs.MaxTime, "max_time", -1, "Maximum test runtime in seconds.")
	flag.StringVar(&parsedArgs.BQResultTable, "bq_result_table", "", "Upload test results to a specified BQ table.")
	flag.Var((*stringSliceFlag)(&parsedArgs.CMakeConfigureExtraArgs), "cmake_configure_extra_args", "Extra arguments for cmake configure command. Can be specified multiple times. Only for C/C++.")
	flag.IntVar(&parsedArgs.InnerJobs, "inner_jobs", defaultInnerJobs, "Number of jobs assigned to each run_tests.py instance (used by outer matrix script).")
}

func main() {
	// initFlags() sets up the command-line flags.
	initFlags()
	// Parse the flags defined by initFlags()
	flag.Parse()

	// Handle the nargs='+' behavior for the language flag.
	if len(parsedArgs.Language) == 1 && strings.Contains(parsedArgs.Language[0], ",") {
		parsedArgs.Language = strings.Split(parsedArgs.Language[0], ",")
	}
	if len(parsedArgs.Language) == 0 {
		fmt.Fprintf(os.Stderr, "Error: -l/--language is required and must specify at least one language.\n")
		os.Exit(1)
	}

	// Set GRPC_ROOT environment variable
	_, currentFile, _, ok := runtime.Caller(0)
	if !ok {
		log.Fatalf("Failed to get current file path for GRPC_ROOT setup.")
	}
	// Assuming run_tests.go is in grpc/tools/run_tests/
	grpcRoot := filepath.Join(filepath.Dir(currentFile), "..", "..")
	os.Setenv("GRPC_ROOT", grpcRoot)
	// Change to root directory as in Python script
	if err := os.Chdir(grpcRoot); err != nil {
		log.Fatalf("Failed to change directory to %s: %v", grpcRoot, err)
	}
	log.Printf("Working directory set to: %s\n", grpcRoot)

	// Apply polling strategies from command line
	if parsedArgs.ForceDefaultPoller {
		pollingStrategies = map[string][]string{}
	} else if parsedArgs.ForceUsePollers != "" {
		pollingStrategies[platformString()] = strings.Split(parsedArgs.ForceUsePollers, ",")
	}

	// Initialize jobrunner's CPU cost measurement flag
	MeasureCPUCosts = parsedArgs.MeasureCPUCosts

	// Load global configs
	runConfig, err := LoadConfig(parsedArgs.Config)
	if err != nil {
		log.Fatalf("Failed to load config %s: %v", parsedArgs.Config, err)
	}

	var activeLanguages []LanguageInterface
	for _, langName := range parsedArgs.Language {
		lang, err := GetLanguage(langName)
		if err != nil {
			log.Fatalf("Unknown language: %s", langName)
		}
		lang.Configure(runConfig, &parsedArgs)
		activeLanguages = append(activeLanguages, lang)
	}

	if len(activeLanguages) != 1 {
		log.Fatalf("Building multiple languages simultaneously is not supported!")
	}

	// Handle --use_docker flag (Mirrors Python's external call)
	if parsedArgs.UseDocker {
		if !parsedArgs.Travis {
			fmt.Println("Seen --use_docker flag, will run tests under docker.")
			fmt.Println("")
			fmt.Println("IMPORTANT: The changes you are testing need to be locally committed")
			fmt.Println("because only the committed changes in the current branch will be")
			fmt.Println("copied to the docker environment.")
			time.Sleep(5 * time.Second)
		}

		dockerfileDirs := make(map[string]struct{})
		for _, lang := range activeLanguages {
			dir, err := lang.DockerfileDir()
			if err != nil && dir != "" { // Only error if dir is non-empty but errored
				log.Fatalf("Error getting Dockerfile directory for language %s: %v", lang.String(), err)
			}
			if dir != "" {
				dockerfileDirs[dir] = struct{}{}
			}
		}

		if len(dockerfileDirs) > 1 {
			log.Fatalf("Languages to be tested require running under different docker images. Found: %v", dockerfileDirs)
		}

		var dockerfileDir string
		if len(dockerfileDirs) == 1 {
			for dir := range dockerfileDirs { // Get the single directory
				dockerfileDir = dir
			}
		} else if len(dockerfileDirs) == 0 {
			log.Fatalf("No Dockerfile directory found for the specified languages.")
		}

		childArgv := []string{}
		for _, arg := range os.Args[1:] {
			if arg != "--use_docker" {
				childArgv = append(childArgv, arg)
			}
		}
		runTestsCmd := fmt.Sprintf("python3 tools/run_tests/run_tests.py %s", strings.Join(childArgv, " "))

		cmd := exec.Command("tools/run_tests/dockerize/build_and_run_docker.sh")
		cmd.Env = os.Environ()
		cmd.Env = append(cmd.Env, fmt.Sprintf("DOCKERFILE_DIR=%s", dockerfileDir))
		cmd.Env = append(cmd.Env, "DOCKER_RUN_SCRIPT=tools/run_tests/dockerize/docker_run.sh")
		cmd.Env = append(cmd.Env, fmt.Sprintf("DOCKER_RUN_SCRIPT_COMMAND=%s", runTestsCmd))
		cmd.Stdout = os.Stdout
		cmd.Stderr = os.Stderr

		err = cmd.Run()
		exitCode := 0
		if err != nil {
			if exitErr, ok := err.(*exec.ExitError); ok {
				exitCode = exitErr.ExitCode()
			} else {
				log.Fatalf("Docker execution failed: %v", err)
			}
		}
		printDebugInfoEpilogue(dockerfileDir)
		os.Exit(exitCode)
	}

	// Check architecture option
	if err := checkArchOption(parsedArgs.Arch); err != nil {
		log.Fatalf("Architecture check failed: %v", err)
	}

	// Collect pre-build steps
	var preBuildJobs []JobSpec
	for _, lang := range activeLanguages {
		jobs, err := lang.PreBuildSteps()
		if err != nil {
			log.Fatalf("Error getting pre-build steps for language %s: %v", lang.String(), err)
		}
		env, err := lang.BuildStepsEnviron()
		if err != nil {
			log.Fatalf("Error getting build steps environ for language %s: %v", lang.String(), err)
		}
		for _, job := range jobs {
			job.TimeoutSeconds = preBuildStepTimeoutSeconds
			// Apply flake_retries specific to pre-build steps (Python sets 2 retries)
			job.FlakeRetries = 2
			job.TimeoutRetries = 0 // No timeout retries for pre-build in Python
			// Merge build steps environ with job's specific environ
			if job.EnvironMap == nil {
				job.EnvironMap = make(map[string]string)
			}
			for k, v := range env {
				job.EnvironMap[k] = v
			}
			preBuildJobs = append(preBuildJobs, job)
		}
	}

	// Collect build steps
	var buildJobs []JobSpec
	for _, lang := range activeLanguages {
		jobs, err := lang.BuildSteps()
		if err != nil {
			log.Fatalf("Error getting build steps for language %s: %v", lang.String(), err)
		}
		env, err := lang.BuildStepsEnviron()
		if err != nil {
			log.Fatalf("Error getting build steps environ for language %s: %v", lang.String(), err)
		}
		for _, job := range jobs {
			// Merge build steps environ with job's specific environ
			if job.EnvironMap == nil {
				job.EnvironMap = make(map[string]string)
			}
			for k, v := range env {
				job.EnvironMap[k] = v
			}
			// Build steps have no timeout or flake retries by default in Python's jobset, set here if needed.
			buildJobs = append(buildJobs, job)
		}
	}

	// Collect post test steps
	var postTestJobs []JobSpec
	for _, lang := range activeLanguages {
		jobs, err := lang.PostTestsSteps()
		if err != nil {
			log.Fatalf("Error getting post-test steps for language %s: %v", lang.String(), err)
		}
		env, err := lang.BuildStepsEnviron()
		if err != nil {
			log.Fatalf("Error getting build steps environ for language %s: %v", lang.String(), err)
		}
		for _, job := range jobs {
			// Merge build steps environ with job's specific environ
			if job.EnvironMap == nil {
				job.EnvironMap = make(map[string]string)
			}
			for k, v := range env {
				job.EnvironMap[k] = v
			}
			postTestJobs = append(postTestJobs, job)
		}
	}

	// Main build and run loop
	errors := _buildAndRun(
		// check_cancelled (lambda: False) - no external cancellation mechanism in this conversion
		func() bool { return false },
		parsedArgs.NewlineOnSuccess,
		parsedArgs.XMLReport,
		parsedArgs.BuildOnly,
		preBuildJobs,
		buildJobs,
		postTestJobs,
		activeLanguages[0], // Only one language supported for now
	)

	if len(errors) == 0 {
		log.Println("SUCCESS: All tests passed")
	} else {
		log.Println("FAILED: Some tests failed")
	}

	if !isUseDockerChild() {
		printDebugInfoEpilogue("") // dockerfile_dir might not be available here, as in Python
	}

	finalExitCode := 0
	for _, e := range errors {
		if e == BuildError {
			finalExitCode |= 1
		} else if e == TestError {
			finalExitCode |= 2
		} else if e == PostTestError {
			finalExitCode |= 4
		}
	}
	os.Exit(finalExitCode)
}

// _buildAndRun is the Go equivalent of Python's _build_and_run function.
func _buildAndRun(
	checkCancelled func() bool,
	newlineOnSuccess bool,
	xmlReport string,
	buildOnly bool,
	preBuildJobs []JobSpec,
	buildJobs []JobSpec,
	postTestJobs []JobSpec,
	language LanguageInterface, // Pass the active language object
) []BuildAndRunError {
	var errors []BuildAndRunError

	// 1. Run Pre-Build Steps
	log.Println("Running pre-build steps...")
	numFailures, preBuildResultSet := Run(
		preBuildJobs,
		1,    // maxjobs=1 for pre-build
		true, // stop_on_failure
		newlineOnSuccess,
		parsedArgs.Travis,
		parsedArgs.QuietSuccess,
		-1, // no overall max_time for pre-build
		parsedArgs.AllowFlakes,
	)
	if numFailures > 0 {
		errors = append(errors, BuildError)
		return errors
	}

	// 2. Run Build Steps
	log.Println("Running build steps...")
	numFailures, buildResultSet := Run(
		buildJobs,
		1,    // maxjobs=1 for build
		true, // stop_on_failure
		newlineOnSuccess,
		parsedArgs.Travis,
		parsedArgs.QuietSuccess,
		-1,    // no overall max_time for build
		false, // no flakes for builds
	)
	if numFailures > 0 {
		errors = append(errors, BuildError)
		return errors
	}

	if buildOnly {
		// Combine preBuildResultSet and buildResultSet for reporting if build_only
		finalBuildResultSet := make(map[string][]JobResult)
		for k, v := range preBuildResultSet {
			finalBuildResultSet[k] = v
		}
		for k, v := range buildResultSet {
			finalBuildResultSet[k] = v
		}
		if xmlReport != "" {
			err := RenderJUnitXMLReport(finalBuildResultSet, xmlReport, parsedArgs.ReportSuiteName, parsedArgs.ReportMultiTarget)
			if err != nil {
				log.Printf("Error rendering JUnit XML report for build_only: %v", err)
			}
		}
		return errors
	}

	// 3. Start Antagonists (if any)
	var antagonists []*exec.Cmd
	for i := 0; i < parsedArgs.Antagonists; i++ {
		cmd := exec.Command("python3", "tools/run_tests/python_utils/antagonist.py")
		cmd.Stdout = os.Stdout // Direct output to console
		cmd.Stderr = os.Stderr
		if err := cmd.Start(); err != nil {
			log.Printf("Failed to start antagonist: %v", err)
		}
		antagonists = append(antagonists, cmd)
	}

	// 4. Start Port Server (if needed)
	var portServerCmd *exec.Cmd
	portServerCmd = exec.Command("python3", filepath.Join(os.Getenv("GRPC_ROOT"), "python_utils", "start_port_server.py"))
	portServerCmd.Stdout = os.Stdout
	portServerCmd.Stderr = os.Stderr
	if err := portServerCmd.Start(); err != nil {
		log.Printf("Warning: Failed to start port server: %v", err)
	} else {
		log.Println("Port server started.")
	}

	numTestFailures := 0
	testResultSet := make(map[string][]JobResult) // Results from test execution
	var allTestSpecs []JobSpec
	var err error

	// 5. Collect Test Specs
	allTestSpecs, err = language.TestSpecs()
	if err != nil {
		log.Printf("Error collecting test specs for language %s: %v", language.String(), err)
		errors = append(errors, TestError)
		return errors
	}

	// Apply regex filter to test specs
	var filteredTestSpecs []JobSpec
	regexCompiled := regexp.MustCompile(parsedArgs.Regex)
	var regexExcludeCompiled *regexp.Regexp
	if parsedArgs.RegexExclude != "" {
		regexExcludeCompiled = regexp.MustCompile(parsedArgs.RegexExclude)
	}

	for _, spec := range allTestSpecs {
		if (parsedArgs.Regex == ".*" || regexCompiled.MatchString(spec.Shortname)) &&
			(parsedArgs.RegexExclude == "" || (regexExcludeCompiled != nil && !regexExcludeCompiled.MatchString(spec.Shortname))) {
			filteredTestSpecs = append(filteredTestSpecs, spec)
		}
	}

	// Handle runs_per_test and sample_percent
	var massagedOneRun []JobSpec
	if parsedArgs.Travis && parsedArgs.MaxTime <= 0 {
		sort.Slice(filteredTestSpecs, func(i, j int) bool {
			return filteredTestSpecs[i].CPUCost < filteredTestSpecs[j].CPUCost
		})
		massagedOneRun = filteredTestSpecs
	} else {
		massagedOneRun = make([]JobSpec, len(filteredTestSpecs))
		copy(massagedOneRun, filteredTestSpecs)
		if !isClose(parsedArgs.SamplePercent, 100.0, 1e-09, 0.0) { // Using custom isClose function
			if parsedArgs.RunsPerTest != 1 {
				log.Fatalf("Can't do sampling (-p) over multiple runs (-n).")
			}
			rand.Seed(time.Now().UnixNano())
			rand.Shuffle(len(massagedOneRun), func(i, j int) {
				massagedOneRun[i], massagedOneRun[j] = massagedOneRun[j], massagedOneRun[i]
			})
			sampleSize := int(float64(len(massagedOneRun)) * parsedArgs.SamplePercent / 100.0)
			massagedOneRun = massagedOneRun[:sampleSize]
			log.Printf("Running %d tests out of %d (~%d%%)", sampleSize, len(filteredTestSpecs), int(parsedArgs.SamplePercent))
		} else {
			rand.Seed(time.Now().UnixNano())
			rand.Shuffle(len(massagedOneRun), func(i, j int) {
				massagedOneRun[i], massagedOneRun[j] = massagedOneRun[j], massagedOneRun[i]
			})
		}
	}

	var allRunsForExecution []JobSpec
	if parsedArgs.RunsPerTest == 0 { // Infinite runs
		if len(massagedOneRun) == 0 {
			log.Fatalf("Must have at least one test for an -n inf run")
		}
		// For an infinite run, `Run` function's context cancellation (maxTime) will be the exit condition.
		// We'll feed jobs as long as the context allows.
		// For a practical slice to pass to `Run`, let's just use `massagedOneRun` and rely on `Run`'s internal loop if infinite.
		// The `Run` function is not designed for truly infinite `jobs` slice.
		// So we loop here to create a very large slice to simulate "infinite" for the `Run` function.
		// A more robust solution would modify `Run` to take a channel of JobSpecs.
		for i := 0; i < 1000000; i++ { // Simulate large number of runs to keep jobrunner busy
			allRunsForExecution = append(allRunsForExecution, massagedOneRun...)
		}
	} else {
		for i := 0; i < parsedArgs.RunsPerTest; i++ {
			allRunsForExecution = append(allRunsForExecution, massagedOneRun...)
		}
	}

	if parsedArgs.QuietSuccess {
		log.Println("START: Running tests quietly, only failing tests will be reported")
	}

	// 6. Run Tests
	numTestFailures, testResultSetMap := Run(
		allRunsForExecution,
		parsedArgs.Jobs,
		parsedArgs.StopOnFailure,
		newlineOnSuccess,
		parsedArgs.Travis,
		parsedArgs.QuietSuccess,
		time.Duration(parsedArgs.MaxTime)*time.Second,
		parsedArgs.AllowFlakes,
	)
	testResultSet = testResultSetMap

	// 7. Process Test Results and Generate Report Messages
	if testResultSet != nil {
		for k, v := range testResultSet {
			numRuns, numFailures := calculateNumRunsFailures(v)
			if numFailures > 0 {
				if numFailures == numRuns {
					log.Printf("FAILED: %s\n", k)
				} else {
					log.Printf("FLAKE: %s [%d/%d runs flaked]\n", k, numFailures, numRuns)
				}
			}
		}
	}

	// 8. Cleanup Antagonists
	for _, antagonist := range antagonists {
		if antagonist.Process != nil {
			log.Printf("Killing antagonist process %d\n", antagonist.Process.Pid)
			if err := antagonist.Process.Kill(); err != nil {
				log.Printf("Error killing antagonist process %d: %v", antagonist.Process.Pid, err)
			}
			// Wait for the process to truly exit
			if _, err := antagonist.Process.Wait(); err != nil {
				log.Printf("Error waiting for antagonist process %d: %v", antagonist.Process.Pid, err)
			}
		}
	}
	// 9. Shut down port server
	if portServerCmd != nil && portServerCmd.Process != nil {
		log.Printf("Killing port server process %d\n", portServerCmd.Process.Pid)
		if err := portServerCmd.Process.Kill(); err != nil {
			log.Printf("Error killing port server process %d: %v", portServerCmd.Process.Pid, err)
		}
		// Wait for the process to truly exit
		if _, err := portServerCmd.Process.Wait(); err != nil {
			log.Printf("Error waiting for port server process %d: %v", portServerCmd.Process.Pid, err)
		}
	}

	// 10. Upload Results to BigQuery (Placeholder)
	if parsedArgs.BQResultTable != "" && testResultSet != nil {
		uploadExtraFields := map[string]string{
			"compiler":       parsedArgs.Compiler,
			"config":         parsedArgs.Config,
			"iomgr_platform": parsedArgs.IOMgrPlatform,
			"language":       parsedArgs.Language[0], // Assuming single language due to current limitation
			"platform":       platformString(),
		}
		log.Printf("TODO: Implement BigQuery upload to table %s with fields: %v\n", parsedArgs.BQResultTable, uploadExtraFields)
		// Here you would use a Go BigQuery client library to upload results.
	}

	// 11. Generate XML Report
	if xmlReport != "" && testResultSet != nil {
		err := RenderJUnitXMLReport(testResultSet, xmlReport, parsedArgs.ReportSuiteName, parsedArgs.ReportMultiTarget)
		if err != nil {
			log.Printf("Error rendering JUnit XML report: %v", err)
		}
	}

	// 12. Run Post-Test Steps
	log.Println("Running post-test steps...")
	numPostTestFailures, _ := Run(
		postTestJobs,
		1,     // maxjobs=1
		false, // stop_on_failure=False
		newlineOnSuccess,
		parsedArgs.Travis,
		parsedArgs.QuietSuccess,
		-1,    // no overall max_time
		false, // no flakes for post-tests
	)

	if numPostTestFailures > 0 {
		errors = append(errors, PostTestError)
	}
	if numTestFailures > 0 {
		errors = append(errors, TestError)
	}

	return errors
}
