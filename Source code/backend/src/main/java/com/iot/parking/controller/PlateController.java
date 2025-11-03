package com.iot.parking.controller;

import org.springframework.http.MediaType;

import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.nio.file.StandardCopyOption;
import java.time.OffsetDateTime;
import java.util.ArrayList;
import java.util.UUID;

import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.web.bind.annotation.*;
import org.springframework.web.multipart.MultipartFile;

import com.iot.parking.model.Plate;
import com.iot.parking.repository.PlateRepository;

import net.sourceforge.tess4j.Tesseract;
import net.sourceforge.tess4j.TesseractException;

@RestController
@RequestMapping("/api/plate")
public class PlateController {
	@Autowired
	PlateRepository repo;
	
	@PostMapping(value = "/upload", consumes = MediaType.MULTIPART_FORM_DATA_VALUE)
	public boolean uploadPlate(@RequestParam("file") MultipartFile file,
            @RequestParam(value="cameraId", required=false) String cameraId,
            @RequestParam(value="timestamp", required=false) String timestampStr) throws IOException, TesseractException {

		String filename = UUID.randomUUID().toString() + ".jpg"; 
	    Path destinationFile = Paths.get("upload-dir").resolve(filename);
	    
	    Files.createDirectories(destinationFile.getParent()); 
	    try (InputStream inputStream = file.getInputStream()) {
	        Files.copy(inputStream, destinationFile, StandardCopyOption.REPLACE_EXISTING);
	    }
	    
	    String path = filename;
	    try {
		    Plate temp = new Plate();
		    Tesseract tes = new Tesseract();
		    tes.setDatapath("D:\\Study_App\\Tesseract\\tessdata");
		    tes.setLanguage("eng");
		    tes.setPageSegMode(7);
		    String number = tes.doOCR(new File(path));
		    
		    temp.setImagePath(path);
		    temp.setNumber(number);
		    temp.setTimestamp(OffsetDateTime.now());
			
		    repo.save(temp);
			return true;
	    }catch(Exception e) {
	    	e.printStackTrace();
	    	return false;
	    }
	}
	
	@GetMapping("/show")
	public ArrayList<Plate> showPlate(){
		return (ArrayList<Plate>) repo.findAll();
	}
}
