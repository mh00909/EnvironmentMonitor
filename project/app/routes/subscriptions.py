from flask import Blueprint, request, jsonify

subscriptions_bp = Blueprint('subscriptions', __name__)

@subscriptions_bp.route('/subscriptions', methods=['GET'])
def get_subscriptions():
    return jsonify({"message": "Subscriptions route works!"})
