import time
from flask import Flask
app = Flask(__name__)

@app.route("/one_second")
def one_second():
    time.sleep(1)
    return "Response_1"

@app.route("/three_seconds")
def three_seconds():
    time.sleep(3)
    return "Response_3"

if __name__ == "__main__":
    app.run(threaded=True)