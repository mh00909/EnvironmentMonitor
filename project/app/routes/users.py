from flask import Blueprint, request, jsonify
from app.database import get_db_connection

users_bp = Blueprint('users', __name__)

@users_bp.route('/users', methods=['POST'])
def add_user():
    data = request.json
    name = data.get('name')
    if not name:
        return jsonify({"error": "Name is required"}), 400

    conn = get_db_connection()
    cursor = conn.cursor()
    try:
        cursor.execute('INSERT INTO users (name) VALUES (?)', (name,))
        conn.commit()
        return jsonify({"id": cursor.lastrowid, "name": name}), 201
    except sqlite3.IntegrityError:
        return jsonify({"error": "User already exists"}), 400
    finally:
        conn.close()

@users_bp.route('/users', methods=['GET'])
def get_users():
    conn = get_db_connection()
    cursor = conn.cursor()
    cursor.execute('SELECT id, name FROM users')
    users = [{"id": row[0], "name": row[1]} for row in cursor.fetchall()]
    conn.close()
    return jsonify(users)
