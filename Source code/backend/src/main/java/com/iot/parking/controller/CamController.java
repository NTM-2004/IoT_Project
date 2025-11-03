package com.iot.parking.controller;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.time.LocalDateTime;

import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.*;
import org.springframework.web.multipart.MultipartFile;

@RestController
@RequestMapping("/api/camera")
public class CamController {
	private static final Path UPLOAD_DIR = Paths.get("uploads");


	public CamController() throws IOException {
		if (!Files.exists(UPLOAD_DIR)) Files.createDirectories(UPLOAD_DIR);
	}


	@PostMapping("/upload")
	public ResponseEntity<?> upload(@RequestParam("file") MultipartFile file,
		@RequestParam(value = "deviceId", required = false) String deviceId) throws IOException {
		if (file.isEmpty()) return ResponseEntity.badRequest().body("empty file");
			String filename = System.currentTimeMillis() + "_" + (deviceId != null ? deviceId + "_" : "") + file.getOriginalFilename();
			Path dest = UPLOAD_DIR.resolve(filename);
			Files.write(dest, file.getBytes());
		return ResponseEntity.status(201).body("uploaded: " + filename + " at " + LocalDateTime.now());
	}
}
