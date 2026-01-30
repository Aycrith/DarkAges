"""
Test Dashboard - Real-time Test Monitoring and Reporting

Provides a web-based dashboard for monitoring test execution,
viewing results, and analyzing trends.

Usage:
    python TestDashboard.py --port 8080
    
Then open http://localhost:8080 in your browser.
"""

import json
import time
from pathlib import Path
from typing import Dict, List, Any
from datetime import datetime
import http.server
import socketserver
import threading


class TestDashboard:
    """
    Web dashboard for test monitoring.
    
    Features:
    - Real-time test execution status
    - Historical results
    - Metric visualization
    - Trend analysis
    """
    
    def __init__(self, reports_dir: str = "tools/testing/reports"):
        self.reports_dir = Path(reports_dir)
        self.reports_dir.mkdir(parents=True, exist_ok=True)
        self.active_tests: Dict[str, Dict] = {}
        self.server = None
    
    def load_reports(self) -> List[Dict]:
        """Load all test reports."""
        reports = []
        
        for report_file in self.reports_dir.glob("*.json"):
            try:
                with open(report_file) as f:
                    report = json.load(f)
                    report["_filename"] = report_file.name
                    reports.append(report)
            except Exception as e:
                print(f"Error loading {report_file}: {e}")
        
        # Sort by timestamp
        reports.sort(key=lambda r: r.get("start_time", 0), reverse=True)
        return reports
    
    def generate_html(self) -> str:
        """Generate dashboard HTML."""
        reports = self.load_reports()
        
        # Calculate statistics
        total_tests = len(reports)
        passed_tests = sum(
            1 for r in reports
            if r.get("summary", {}).get("error_count", 0) == 0
        )
        
        recent_reports = reports[:10]  # Last 10 tests
        
        html = f"""
<!DOCTYPE html>
<html>
<head>
    <title>DarkAges MMO - Test Dashboard</title>
    <style>
        body {{
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
            margin: 0;
            padding: 20px;
            background: #1a1a2e;
            color: #eee;
        }}
        .header {{
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            padding: 30px;
            border-radius: 10px;
            margin-bottom: 30px;
        }}
        .header h1 {{
            margin: 0;
            font-size: 2.5em;
        }}
        .stats {{
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
            gap: 20px;
            margin-bottom: 30px;
        }}
        .stat-card {{
            background: #16213e;
            padding: 20px;
            border-radius: 10px;
            text-align: center;
        }}
        .stat-value {{
            font-size: 2.5em;
            font-weight: bold;
            color: #667eea;
        }}
        .stat-label {{
            color: #888;
            margin-top: 5px;
        }}
        .section {{
            background: #16213e;
            padding: 20px;
            border-radius: 10px;
            margin-bottom: 20px;
        }}
        .section h2 {{
            margin-top: 0;
            color: #667eea;
        }}
        table {{
            width: 100%;
            border-collapse: collapse;
        }}
        th, td {{
            text-align: left;
            padding: 12px;
            border-bottom: 1px solid #333;
        }}
        th {{
            color: #888;
            font-weight: 600;
        }}
        .pass {{
            color: #4ade80;
            font-weight: bold;
        }}
        .fail {{
            color: #f87171;
            font-weight: bold;
        }}
        .timestamp {{
            color: #888;
            font-size: 0.9em;
        }}
        .metric-bar {{
            background: #333;
            height: 20px;
            border-radius: 10px;
            overflow: hidden;
        }}
        .metric-fill {{
            height: 100%;
            background: linear-gradient(90deg, #667eea, #764ba2);
            transition: width 0.3s ease;
        }}
        pre {{
            background: #0f0f23;
            padding: 15px;
            border-radius: 5px;
            overflow-x: auto;
        }}
    </style>
</head>
<body>
    <div class="header">
        <h1>DarkAges MMO - Test Dashboard</h1>
        <p>Comprehensive Testing & Monitoring</p>
    </div>
    
    <div class="stats">
        <div class="stat-card">
            <div class="stat-value">{total_tests}</div>
            <div class="stat-label">Total Test Runs</div>
        </div>
        <div class="stat-card">
            <div class="stat-value" style="color: #4ade80;">{passed_tests}</div>
            <div class="stat-label">Passed</div>
        </div>
        <div class="stat-card">
            <div class="stat-value" style="color: #f87171;">{total_tests - passed_tests}</div>
            <div class="stat-label">Failed</div>
        </div>
        <div class="stat-card">
            <div class="stat-value">
                {round((passed_tests / total_tests * 100) if total_tests > 0 else 0)}%
            </div>
            <div class="stat-label">Success Rate</div>
        </div>
    </div>
    
    <div class="section">
        <h2>Recent Test Results</h2>
        <table>
            <tr>
                <th>Test Name</th>
                <th>Status</th>
                <th>Duration</th>
                <th>Metrics</th>
                <th>Errors</th>
                <th>Timestamp</th>
            </tr>
"""
        
        for report in recent_reports:
            test_name = report.get("test_name", "Unknown")
            error_count = report.get("summary", {}).get("error_count", 0)
            status = "PASS" if error_count == 0 else "FAIL"
            status_class = "pass" if error_count == 0 else "fail"
            duration = report.get("duration_seconds", 0)
            metrics_count = report.get("summary", {}).get("total_metrics", 0)
            timestamp = datetime.fromtimestamp(report.get("end_time", 0)).strftime("%Y-%m-%d %H:%M:%S")
            
            html += f"""
            <tr>
                <td>{test_name}</td>
                <td class="{status_class}">{status}</td>
                <td>{duration:.2f}s</td>
                <td>{metrics_count}</td>
                <td>{error_count}</td>
                <td class="timestamp">{timestamp}</td>
            </tr>
"""
        
        html += """
        </table>
    </div>
"""
        
        # Add detailed report for most recent
        if reports:
            latest = reports[0]
            html += f"""
    <div class="section">
        <h2>Latest Test Details: {latest.get("test_name", "Unknown")}</h2>
        <pre>{json.dumps(latest, indent=2)}</pre>
    </div>
"""
        
        html += """
    <div class="section">
        <h2>Performance Trends</h2>
        <p>Coming soon: Metric visualization and trend analysis</p>
    </div>
    
    <script>
        // Auto-refresh every 30 seconds
        setTimeout(() => location.reload(), 30000);
    </script>
</body>
</html>
"""
        return html
    
    def start_server(self, port: int = 8080):
        """Start the dashboard server."""
        dashboard = self
        
        class Handler(http.server.SimpleHTTPRequestHandler):
            def do_GET(self):
                if self.path == "/":
                    self.send_response(200)
                    self.send_header("Content-type", "text/html")
                    self.end_headers()
                    self.wfile.write(dashboard.generate_html().encode())
                elif self.path == "/api/reports":
                    self.send_response(200)
                    self.send_header("Content-type", "application/json")
                    self.end_headers()
                    self.wfile.write(json.dumps(dashboard.load_reports()).encode())
                else:
                    self.send_error(404)
            
            def log_message(self, format, *args):
                pass  # Suppress logs
        
        self.server = socketserver.TCPServer(("", port), Handler)
        
        print(f"Starting dashboard server on http://localhost:{port}")
        print("Press Ctrl+C to stop")
        
        try:
            self.server.serve_forever()
        except KeyboardInterrupt:
            print("\nShutting down...")
            self.server.shutdown()


class ConsoleDashboard:
    """
    Console-based dashboard for terminal monitoring.
    """
    
    def __init__(self, reports_dir: str = "tools/testing/reports"):
        self.reports_dir = Path(reports_dir)
    
    def display_summary(self):
        """Display test summary in console."""
        reports = []
        for report_file in self.reports_dir.glob("*.json"):
            try:
                with open(report_file) as f:
                    reports.append(json.load(f))
            except:
                pass
        
        if not reports:
            print("No test reports found.")
            return
        
        total = len(reports)
        passed = sum(1 for r in reports if r.get("summary", {}).get("error_count", 0) == 0)
        
        print("=" * 70)
        print("DARKAGES MMO - TEST SUMMARY")
        print("=" * 70)
        print(f"Total Tests: {total}")
        print(f"Passed: {passed} ({passed/total*100:.1f}%)")
        print(f"Failed: {total - passed}")
        print("=" * 70)
        print()
        print("Recent Tests:")
        print(f"{'Test Name':<30} {'Status':<10} {'Duration':<12} {'Errors':<10}")
        print("-" * 70)
        
        for report in sorted(reports, key=lambda r: r.get("end_time", 0), reverse=True)[:10]:
            name = report.get("test_name", "Unknown")[:28]
            errors = report.get("summary", {}).get("error_count", 0)
            status = "[PASS]" if errors == 0 else "[FAIL]"
            duration = f"{report.get('duration_seconds', 0):.2f}s"
            
            print(f"{name:<30} {status:<10} {duration:<12} {errors:<10}")
        
        print("=" * 70)


# -------------------------------------------------------------------------
# Main Entry Point
# -------------------------------------------------------------------------

def main():
    """Main entry point."""
    import argparse
    
    parser = argparse.ArgumentParser(description="Test Dashboard")
    parser.add_argument("--port", type=int, default=8080, help="Web server port")
    parser.add_argument("--console", action="store_true", help="Console mode only")
    
    args = parser.parse_args()
    
    if args.console:
        dashboard = ConsoleDashboard()
        dashboard.display_summary()
    else:
        dashboard = TestDashboard()
        dashboard.start_server(args.port)


if __name__ == "__main__":
    main()
