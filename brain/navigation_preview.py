def build_navigation_preview_request(
    candidates,
) -> dict | None:
    preview_candidates = []

    for candidate in candidates:
        action = candidate.action

        if action.get("action") != "move_to":
            continue

        preview_candidates.append({
            "id": len(preview_candidates) + 1,
            "action": "move_to",
            "target": list(action["target"]),
        })

    if not preview_candidates:
        return None

    return {
        "type": "preview_request",
        "candidates": preview_candidates,
    }


def validate_navigation_preview_response(
    request: dict,
    response: dict,
) -> None:
    if response.get("type") != "preview_response":
        raise ValueError("Expected a navigation preview response.")

    expected_ids = {
        candidate["id"]
        for candidate in request["candidates"]
    }
    previews = response.get("previews")

    if not isinstance(previews, list):
        raise ValueError("Navigation previews must be a list.")

    received_ids = [preview.get("id") for preview in previews]

    if len(received_ids) != len(set(received_ids)):
        raise ValueError("Navigation preview IDs must be unique.")

    if set(received_ids) != expected_ids:
        raise ValueError("Navigation preview IDs do not match the request.")

    for preview in previews:
        reachable = preview.get("reachable")
        path_length = preview.get("path_length")
        next_step = preview.get("next_step")

        if not isinstance(reachable, bool):
            raise ValueError("Navigation reachability must be Boolean.")

        if reachable:
            if not isinstance(path_length, int) or path_length < 0:
                raise ValueError("Reachable previews need a valid path length.")

            if path_length > 0 and (
                not isinstance(next_step, list)
                or len(next_step) != 2
            ):
                raise ValueError("Non-empty paths need a next step.")
        elif path_length is not None or next_step is not None:
            raise ValueError("Unreachable previews cannot contain route data.")


def navigation_previews_by_target(
    request: dict,
    response: dict,
) -> dict[tuple[int, int], dict]:
    validate_navigation_preview_response(request, response)

    requested_by_id = {
        candidate["id"]: candidate
        for candidate in request["candidates"]
    }

    return {
        tuple(requested_by_id[preview["id"]]["target"]): preview
        for preview in response["previews"]
    }
