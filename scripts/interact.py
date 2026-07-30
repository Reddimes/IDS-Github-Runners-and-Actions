import os
import sys
import json
import wexpect
import datetime

def fuzzy_match(actual, expected):
    return str(actual).strip().lower() == str(expected).strip().lower()

def run_test_suite(manifest_path):
    with open(manifest_path, 'r') as f:
        data = json.load(f)

    target_exe = data['metadata']['target_exe']
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

    log_file_path = os.path.join("tests", "interaction_trace.log")
    
    try:
        # Spawn the target process
        child = wexpect.spawn(target_exe, encoding="utf-8")
        
        with open(log_file_path, "w") as log_file:
            log_file.write(f"--- Interaction Session Start ---\n")
            log_file.write(f"Target: {target_exe}\n")
            log_file.write(f"Timestamp: {data['metadata']['timestamp'] if 'timestamp' in data['metadata'] else datetime.datetime.now().isoformat()}\n")
            log_file.write("-" * 30 + "\n")

            for tc in test_cases:
                tc_id = tc['id']
                input_str = tc['input']
                expected_str = tc['expected']
                
                log_file.write(f"\n[Test Case {tc_id}] Input: '{input_str}' | Expected: '{expected_str}'\n")
                print(f"Running {tc_id}...")

                try:
                    # Wait for prompt
                    child.expect(config['prompt_pattern'])
                    log_file.write(f"PROMPT: {child.before}\n")

                    # Send input
                    child.sendline(input_str)
                    log_file.write(f"SENT: {input_str}\n")

                    # Wait for the next prompt or EOF (to see the result)
                    # Since the program might be in a loop, we expect the prompt again 
                    # unless it's the last one and it exits.
                    # For simplicity, we'll expect either the prompt or EOF.
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

            # Clean up
            if config.get('exit_command'):
                log_file.write(f"\nSending exit command: {config['exit_command']}\n")
                child.sendline(config['exit_command'])
                child.expect(wexpect.EOF)
            else:
                child.terminate(force=True)
            
            log_file.write(f"\n--- Interaction Session End ---\n")
            log_file.write(f"Final Results: {results['summary']}\n")

    except Exception as e:
        log_file.write(f"CRITICAL ERROR: {str(e)}\n")
        print(f"Critical Failure: {e}")
        sys.exit(1)

    # Finalize results
    results['summary']['score'] = (results['summary']['passed'] / results['summary']['total']) * 100
    
    with open(log_file_path, "a") as log_file:
        log_file.write("-" * 30 + "\n")
        log_file.write(f"Final Score: {results['summary']['score']}%\n")

    with open(os.path.join("tests", "test_results.json"), "w") as f:
        json.dump(results, f, indent=2)

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
