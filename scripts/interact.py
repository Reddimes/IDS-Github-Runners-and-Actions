import os
import json
import datetime
import sys
import wexpect

def fuzzy_match(actual, expected):
    return str(actual).strip().lower() == str(expected).strip().lower()

def run_test_suite(manifest_path):
    # Get the directory where the script is located
    script_dir = os.path.dirname(os.path.abspath(__file__))
    # The project root is one level up from scripts/
    project_root = os.path.dirname(script_dir)
    
    # Define paths relative to the project root
    tests_dir = os.path.join(project_root, "tests")
    log_file_path = os.path.join(tests_dir, "interaction_trace.log")
    results_file_path = os.path.join(tests_dir, "test_results.json")
    
    os.makedirs(tests_dir, exist_ok=True)
    
    # Convert manifest_path to absolute path to be safe
    manifest_path = os.path.abspath(manifest_path)
    print(f"DEBUG: Manifest path: {manifest_path}")
    
    if not os.path.exists(manifest_path):
        print(f"Error: Manifest file not found: {manifest_path}")
        sys.exit(1)

    with open(manifest_path, 'r') as f:
        data = json.load(f)
    
    target_exe = data['metadata']['target_exe']
    # Resolve target_exe relative to the project root
    if not os.path.isabs(target_exe):
        target_exe = os.path.normpath(os.path.join(project_root, target_exe))
    else:
        target_exe = os.path.normpath(target_exe)
    print(f"DEBUG: Target exe path: {target_exe}")
    
    if os.path.exists(target_exe):
        print(f"DEBUG: Target executable found: {target_exe}")
    else:
        print(f"Error: Target executable not found: {target_exe}")
        sys.exit(1)

    config = data['config']
    test_cases = data['test_cases']
    
    results = {
        "test_name": data['metadata']['test_name'],
        "timestamp": datetime.datetime.now().isoformat(),
        "summary": {
            "total": len(test_cases),
            "passed": 0,
            "failed": 0,
            "score": 0.0
        },
        "details": []
    }
    
    try:
        with open(log_file_path, "w") as log_file:
            log_file.write(f"--- Test Suite Started ---\n")
            log_file.write(f"Target: {target_exe}\n")
            log_file.write(f"Timestamp: {data['metadata'].get('timestamp', datetime.datetime.now().isoformat())}\n")
            log_file.write("-" * 30 + "\n")
            
            for tc in test_cases:
                tc_id = tc['id']
                input_str = tc['input']
                expected_str = tc['expected']
                
                log_file.write(f"\n[Test Case {tc_id}] Input: '{input_str}' | Expected: '{expected_str}'\n")
                print(f"Running {tc_id}...")
                
                child = None
                try:
                    child = wexpect.spawn(target_exe, encoding="utf-8")
                    log_file.write(f"\n[Test Case {tc_id}] Starting new process...\n")
                    
                    # Wait for prompt
                    child.expect(config['prompt_pattern'])
                    log_file.write(f"PROMPT: {child.before}\n")
                    
                    # Send input
                    child.sendline(input_str)
                    log_file.write(f"SENT: {input_str}\n")
                    
                    # Wait for the next prompt or EOF
                    try:
                        child.expect([config['prompt_pattern'], wexpect.EOF], timeout=2)
                    except wexpect.TIMEOUT:
                        log_file.write("TIMEOUT waiting for next prompt/EOF\n")
                    
                    actual_output = child.before.strip()
                    log_file.write(f"ACTUAL OUTPUT: {actual_output}\n")
                    
                    # Match logic
                    is_match = fuzzy_match(actual_output, expected_str) if config.get('fuzzy_match') else (actual_output == expected_str)
                    
                    if is_match:
                        results['summary']['passed'] += 1
                        status = "passed"
                        log_file.write(f"RESULT: PASS\n")
                    else:
                        results['summary']['failed'] += 1
                        status = "failed"
                        log_file.write(f"RESULT: FAIL (Expected: '{expected_str}', Got: '{actual_output}')\n")
                    
                    results['details'].append({
                        "id": tc_id,
                        "status": status,
                        "input": input_str,
                        "expected": expected_str,
                        "actual": actual_output
                    })
                
                except Exception as e:
                    log_file.write(f"ERROR in test case {tc_id}: {str(e)}\n")
                    results['summary']['failed'] += 1
                    results['details'].append({
                        "id": tc_id,
                        "status": "error",
                        "error": str(e)
                    })
                    print(f"Error in {tc_id}: {e}")
                finally:
                    # CLEANUP PER TEST CASE
                    if child:
                        try:
                            if config.get('exit_command'):
                                try:
                                    child.sendline(config['exit_command'])
                                    child.expect(wexpect.EOF, timeout=2)
                                except Exception:
                                    child.terminate(force=True)
                            else:
                                child.terminate(force=True)
                        except Exception:
                            pass
            
            # Finalize session
            log_file.write(f"\n--- Interaction Session End ---\n")
            log_file.write(f"Final Results: {results['summary']}\n")
            log_file.write("-" * 30 + "\n")
            score = (results['summary']['passed'] / results['summary']['total'] * 100) if results['summary']['total'] > 0 else 0
            log_file.write(f"Final Score: {score}%\n")
            results['summary']['score'] = score
            
        # Finalize results to JSON
        with open(results_file_path, "w") as f:
            json.dump(results, f, indent=2)
            
    except Exception as e:
        print(f"Setup or Execution Failure: {e}")
        sys.exit(1)

                
        # Finalize results to JSON
        with open(results_file_path, "w") as f:
            json.dump(results, f, indent=2)
            
    except Exception as e:
        print(f"Setup or Execution Failure: {e}")
        sys.exit(1)
    
    print(f"\nTest Suite Complete: {results['summary']['passed']}/{results['summary']['total']} passed.")
    print(f"Final Score: {results['summary']['score']}%")
    
    if results['summary']['passed'] < len(test_cases):
        sys.exit(1)
    else:
        sys.exit(0)

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python interact.py <manifest_path>")
        sys.exit(1)
    run_test_suite(sys.argv[1])
