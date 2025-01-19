from app.main import create_app, socketio
from app.database import init_db

init_db()

app, socketio = create_app()

if __name__ == '__main__':
    socketio.run(app, host='0.0.0.0', port=5000, debug=True)
