from flask import Flask, redirect, render_template, session, url_for
from flask_socketio import SocketIO
from app.mqtt_handler import setup_mqtt
from app.extensions import socketio



def create_app():
    app = Flask(__name__)
    app.config['SECRET_KEY'] = 'your_secret_key'
    setup_mqtt()

    # Inicjalizacja SocketIO z aplikacją
    socketio.init_app(app)
    # Rejestracja blueprintów
    from .routes.auth import auth_bp
    from .routes.subscriptions import subscriptions_bp
    from .routes.ota import ota_bp
    from .routes.devices import devices_bp
 
    app.register_blueprint(auth_bp, url_prefix='/auth')
    app.register_blueprint(subscriptions_bp, url_prefix='/subscriptions')
    app.register_blueprint(ota_bp, url_prefix='/ota')
    app.register_blueprint(devices_bp)




    return app, socketio
